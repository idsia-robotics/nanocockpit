-------------------------------------------------------------------------------
--
-- author: Elia Cereda <eliacereda@gmail.com>
-- Copyright (c) 2023, Elia Cereda
-- All rights reserved
--
-- Version: 1.0
--

----------------------------------------
-- do not modify this table
local debug_level = {
    DISABLED = 0,
    LEVEL_1  = 1,
    LEVEL_2  = 2
}

----------------------------------------
-- set this DEBUG to debug_level.LEVEL_1 to enable printing debug_level info
-- set it to debug_level.LEVEL_2 to enable really verbose printing
-- set it to debug_level.DISABLED to disable debug printing
-- note: this will be overridden by user's preference settings
local DEBUG = debug_level.LEVEL_1

-- a table of our default settings - these can be changed by changing
-- the preferences through the GUI or command-line; the Lua-side of that
-- preference handling is at the end of this script file
local default_settings =
{
    debug_level  = DEBUG,
    enabled      = true, -- whether this dissector is enabled or not
    cpx_function = 0x06, -- default TCP port number for CPX
}

local dprint = function() end
local dprint2 = function() end
local function resetDebugLevel()
    if default_settings.debug_level > debug_level.DISABLED then
        dprint = function(...)
            print(table.concat({"Lua: ", ...}," "))
        end

        if default_settings.debug_level > debug_level.LEVEL_1 then
            dprint2 = dprint
        end
    else
        dprint = function() end
        dprint2 = dprint
    end
end
-- call it now
resetDebugLevel()

--------------------------------------------------------------------------------
-- creates a Proto object, but doesn't register it yet
local cpx_streamer_proto = Proto("cpx_streamer", "CPX Streamer")


----------------------------------------
-- a function to convert tables of enumerated types to value-string tables
-- i.e., from { "name" = number } to { number = "name" }
local function makeValString(enumTable)
    local t = {}
    for name,num in pairs(enumTable) do
        t[num] = name
    end
    return t
end

local streamer_command = {
    BUFFER_BEGIN           = 0x10,
    BUFFER_DATA            = 0x11,
}
local streamer_command_valstr = makeValString(streamer_command)

local streamer_type = {
    IMAGE          = 0x01,
    INFERENCE      = 0xF0,
    FOG_BUFFER     = 0xF1,
}
local streamer_type_valstr = makeValString(streamer_type)


----------------------------------------
-- a table of all of our Protocol's fields
local header_fields = {
    command   = ProtoField.uint8("cpx_streamer.command", "Command", base.HEX, streamer_command_valstr),

    type      = ProtoField.uint8("cpx_streamer.begin.type", "Type", base.HEX, streamer_type_valstr),
    size      = ProtoField.uint32("cpx_streamer.begin.size", "Size", base.UNIT_STRING, {" byte", " bytes"}),
    checksum  = ProtoField.uint32("cpx_streamer.begin.checksum", "Checksum", base.HEX),
    padding   = ProtoField.uint16("cpx_streamer.begin.padding", "Padding", base.HEX),

    padding1   = ProtoField.uint16("cpx_streamer.data.padding1", "Padding", base.HEX),
    padding2   = ProtoField.uint8("cpx_streamer.data.padding2", "Padding", base.HEX),
}

-- register the ProtoFields
cpx_streamer_proto.fields = header_fields

dprint2("cpx_streamer_proto ProtoFields registered")

--------------------------------------------------------------------------------
-- We want to have our protocol dissection invoked for a specific CPX function,
-- so get the CPX dissector table and add our protocol to it.
local function enableDissector()
    -- using DissectorTable:set() removes existing dissector(s), whereas the
    -- DissectorTable:add() one adds ours before any existing ones, but
    -- leaves the other ones alone, which is better
    DissectorTable.get("cpx.function"):add(default_settings.cpx_function, cpx_streamer_proto)
    DissectorTable.get("cpx_udp.function"):add(default_settings.cpx_function, cpx_streamer_proto)
end

local function disableDissector()
    DissectorTable.get("cpx.function"):remove(default_settings.cpx_function, cpx_streamer_proto)
    DissectorTable.get("cpx_udp.function"):remove(default_settings.cpx_function, cpx_streamer_proto)
end

---------------------------------------
-- This function will be invoked by Wireshark during initialization, such as
-- at program start and loading a new file
function cpx_streamer_proto.init()
    enableDissector()
end

-- this holds the plain "data" Dissector, in case we can't dissect it as Netlink
local data = Dissector.get("data")

--------------------------------------------------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "cpx_streamer_proto.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function cpx_streamer_proto.dissector(tvbuf, pktinfo, root)
    dprint2("cpx_streamer_proto.dissector called")

    -- get the length of the packet buffer (Tvb).
    local pktlen = tvbuf:len()

    local tree = root:add(cpx_streamer_proto, tvbuf)
    
    local command_tvbr = tvbuf:range(0, 1)
    tree:add_le(header_fields.command, command_tvbr)
    
    local command_value = command_tvbr:le_uint()
    local command_label = streamer_command_valstr[command_value]

    pktinfo.cols.info:append(string.format(", Streamer %s", command_label))
    
    local payload_tvbr
    
    if command_value == streamer_command.BUFFER_BEGIN then
        local type_tvbr = tvbuf:range(1, 1)
        local size_tvbr = tvbuf:range(2, 4)
        local checksum_tvbr = tvbuf:range(6, 4)
        local padding_tvbr = tvbuf:range(10, 2)
        payload_tvbr = tvbuf:range(12, pktlen - 12)

        tree:add_le(header_fields.type, type_tvbr)
        tree:add_le(header_fields.size, size_tvbr)
        tree:add_le(header_fields.checksum, checksum_tvbr)
        tree:add_le(header_fields.padding, padding_tvbr)

        local type_label = streamer_type_valstr[type_tvbr:le_uint()]
        pktinfo.cols.info:append(
            string.format(" [%s, Size=%d]", 
            type_label,
            size_tvbr:le_uint()
        ))
    elseif command_value == streamer_command.BUFFER_DATA then
        local padding1_tvbr = tvbuf:range(1, 2)
        local padding2_tvbr = tvbuf:range(3, 1)
        payload_tvbr = tvbuf:range(4, pktlen - 4)

        tree:add_le(header_fields.padding1, padding1_tvbr)
        tree:add_le(header_fields.padding2, padding2_tvbr)
    end
    data:call(payload_tvbr:tvb(), pktinfo, root)
end

--------------------------------------------------------------------------------
-- preferences handling stuff
--------------------------------------------------------------------------------

local debug_pref_enum = {
    { 1,  "Disabled", debug_level.DISABLED },
    { 2,  "Level 1",  debug_level.LEVEL_1  },
    { 3,  "Level 2",  debug_level.LEVEL_2  },
}

----------------------------------------
-- register our preferences
cpx_streamer_proto.prefs.enabled     = Pref.bool("Dissector enabled", default_settings.enabled,
                                        "Whether the CPX Streamer dissector is enabled or not")

cpx_streamer_proto.prefs.debug       = Pref.enum("Debug", default_settings.debug_level,
                                        "The debug printing level", debug_pref_enum)

----------------------------------------
-- the function for handling preferences being changed
function cpx_streamer_proto.prefs_changed()
    dprint2("prefs_changed called")

    default_settings.debug_level = cpx_streamer_proto.prefs.debug
    resetDebugLevel()

    if default_settings.enabled ~= cpx_streamer_proto.prefs.enabled then
        default_settings.enabled = cpx_streamer_proto.prefs.enabled
        if default_settings.enabled then
            enableDissector()
        else
            disableDissector()
        end
        -- have to reload the capture file for this type of change
        reload()
    end

end

dprint2("pcapfile Prefs registered")
