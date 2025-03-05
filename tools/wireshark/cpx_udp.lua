-------------------------------------------------------------------------------
--
-- author: Elia Cereda <eliacereda@gmail.com>
-- Copyright (c) 2023, Elia Cereda
-- All rights reserved
--
-- Version: 1.0
--
-- BACKGROUND:
-- This is an example Lua script for a protocol dissector. The purpose of this script is two-fold:
--   * To provide a reference tutorial for others writing Wireshark dissectors in Lua
--   * To test various functions being called in various ways, so this script can be used in the test-suites
-- I've tried to meet both of those goals, but it wasn't easy. No doubt some folks will wonder why some
-- functions are called some way, or differently than previous invocations of the same function. I'm trying to
-- to show both that it can be done numerous ways, but also I'm trying to test those numerous ways, and my more
-- immediate need is for test coverage rather than tutorial guide. (the Lua API is sorely lacking in test scripts)
--
-- OVERVIEW:
-- This script creates an elementary dissector for DNS. It's neither comprehensive nor error-free with regards
-- to the DNS protocol. That's OK. The goal isn't to fully dissect DNS properly - Wireshark already has a good
-- DNS dissector built-in. We don't need another one. We also have other example Lua scripts, but I don't think
-- they do a good job of explaining things, and the nice thing about this one is getting capture files to
-- run it against is trivial. (plus I uploaded one)
--
-- HOW TO RUN THIS SCRIPT:
-- Wireshark and Tshark support multiple ways of loading Lua scripts: through a dofile() call in init.lua,
-- through the file being in either the global or personal plugins directories, or via the command line.
-- See the Wireshark USer's Guide chapter on Lua (https://www.wireshark.org/docs/wsdg_html_chunked/wsluarm.html#wsluarm_intro).
-- Once the script is loaded, it creates a new protocol named "MyDNS" (or "MYDNS" in some places).  If you have
-- a capture file with DNS packets in it, simply select one in the Packet List pane, right-click on it, and
-- select "Decode As ...", and then in the dialog box that shows up scroll down the list of protocols to one
-- called "MYDNS", select that and click the "ok" or "apply" button.  Voila`, you're now decoding DNS packets
-- using the simplistic dissector in this script.  Another way is to download the capture file made for
-- this script, and open that - since the DNS packets in it use UDP port 65333 (instead of the default 53),
-- and since the MyDNS protocol in this script has been set to automatically decode UDP port 65333, it will
-- automagically do it without doing "Decode As ...".
--
----------------------------------------
-- do not modify this table
local debug_level = {
    DISABLED = 0,
    LEVEL_1  = 1,
    LEVEL_2  = 2
}

-- set this DEBUG to debug_level.LEVEL_1 to enable printing debug_level info
-- set it to debug_level.LEVEL_2 to enable really verbose printing
-- note: this will be overridden by user's preference settings
local DEBUG = debug_level.LEVEL_1

local default_settings =
{
    debug_level  = DEBUG,
    enabled      = true, -- whether this dissector is enabled or not
    port         = 5000, -- default UDP port number for CPX
    max_msg_len  = 4092, -- max length of CPX message
    subdissect   = true, -- whether to call sub-dissector or not
}

local dprint = function() end
local dprint2 = function() end
local function reset_debug_level()
    if default_settings.debug_level > debug_level.DISABLED then
        dprint = function(...)
            print(table.concat({"Lua:", ...}," "))
        end

        if default_settings.debug_level > debug_level.LEVEL_1 then
            dprint2 = dprint
        end
    end
end
-- call it now
reset_debug_level()

dprint2("Wireshark version = ", get_version())
dprint2("Lua version = ", _VERSION)

----------------------------------------
-- Unfortunately, the older Wireshark/Tshark versions have bugs, and part of the point
-- of this script is to test those bugs are now fixed.  So we need to check the version
-- end error out if it's too old.
local major, minor, micro = get_version():match("(%d+)%.(%d+)%.(%d+)")
if major and tonumber(major) <= 1 and ((tonumber(minor) <= 10) or (tonumber(minor) == 11 and tonumber(micro) < 3)) then
        error(  "Sorry, but your Wireshark/Tshark version ("..get_version()..") is too old for this script!\n"..
                "This script needs Wireshark/Tshark version 1.11.3 or higher.\n" )
end

-- more sanity checking
-- verify we have the ProtoExpert class in wireshark, as that's the newest thing this file uses
assert(ProtoExpert.new, "Wireshark does not have the ProtoExpert class, so it's too old - get the latest 1.11.3 or higher")

----------------------------------------

----------------------------------------
-- creates a Proto object, but doesn't register it yet
local cpx_proto = Proto("cpx_udp","CPX UDP")

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

local cpx_target = {
    STM32     = 0x1,
    ESP32     = 0x2,
    WIFI_HOST = 0x3,
    GAP      = 0x4,
}
local cpx_target_valstr = makeValString(cpx_target)

local cpx_last_packet = {
    YES = 0x1,
    NO = 0x0,
}
local cpx_last_packet_valstr = makeValString(cpx_last_packet)

local cpx_function = {
    SYSTEM     = 0x1,
    CONSOLE    = 0x2,
    CRTP       = 0x3,
    WIFI_CTRL  = 0x4,
    APP        = 0x5,

    STREAMER   = 0x6,

    TEST       = 0xE,
    BOOTLOADER = 0xF,
}
local cpx_function_valstr = makeValString(cpx_function)

local cpx_version = {
    [0] = 0x0,
    [1] = 0x1,
    [2] = 0x2,
    [3] = 0x3,
}
local cpx_version_valstr = makeValString(cpx_version)

local header_fields = {
    sequence    = ProtoField.uint16("cpx_udp.sequence", "Sequence number", base.DEC),

    destination = ProtoField.uint16("cpx_udp.destination", "Destination", base.HEX, cpx_target_valstr, 0x0007),
    source      = ProtoField.uint16("cpx_udp.source", "Source", base.HEX, cpx_target_valstr, 0x0038),
    last_packet = ProtoField.uint16("cpx_udp.last_packet", "Last packet", base.DEC, cpx_last_packet_valstr, 0x0040),
    reserved    = ProtoField.uint16("cpx_udp.reserved", "Reserved", base.DEC, {}, 0x0080),
    function_   = ProtoField.uint16("cpx_udp.function", "Function", base.HEX, cpx_function_valstr, 0x3F00),
    version     = ProtoField.uint16("cpx_udp.version", "Version", base.DEC, cpx_version_valstr, 0xC000),
}

----------------------------------------
-- this actually registers the ProtoFields above, into our new Protocol
-- in a real script I wouldn't do it this way; I'd build a table of fields programmatically
-- and then set dns.fields to it, so as to avoid forgetting a field
cpx_proto.fields = header_fields

----------------------------------------
-- we don't just want to display our protocol's fields, we want to access the value of some of them too!
-- There are several ways to do that.  One is to just parse the buffer contents in Lua code to find
-- the values.  But since ProtoFields actually do the parsing for us, and can be retrieved using Field
-- objects, it's kinda cool to do it that way. So let's create some Fields to extract the values.
-- The following creates the Field objects, but they're not 'registered' until after this script is loaded.
-- Also, these lines can't be before the 'dns.fields = ...' line above, because the Field.new() here is
-- referencing fields we're creating, and they're not "created" until that line above.
-- Furthermore, you cannot put these 'Field.new()' lines inside the dissector function.
-- Before Wireshark version 1.11, you couldn't even do this concept (of using fields you just created).
local destination_field = Field.new('cpx_udp.destination')
local source_field = Field.new('cpx_udp.source')
local function_field = Field.new('cpx_udp.function')

----------------------------------------
-- create some expert info fields (this is new functionality in 1.11.3)
-- Expert info fields are very similar to proto fields: they're tied to our protocol,
-- they're created in a similar way, and registered by setting a 'experts' field to
-- a table of them just as proto fields were put into the 'dns.fields' above
-- The old way of creating expert info was to just add it to the tree, but that
-- didn't let the expert info be filterable in wireshark, whereas this way does

-- some error expert info's
local ef_too_short = ProtoExpert.new(
    "cpx_udp.too_short.expert", "CPX UDP packet too short",
    expert.group.MALFORMED, expert.severity.ERROR
)

cpx_proto.experts = { ef_too_short }

--------------------------------------------------------------------------------
-- preferences handling stuff
--------------------------------------------------------------------------------

-- a "enum" table for our enum pref, as required by Pref.enum()
-- having the "index" number makes ZERO sense, and is completely illogical
-- but it's what the code has expected it to be for a long time. Ugh.
local debug_pref_enum = {
    { 1,  "Disabled", debug_level.DISABLED },
    { 2,  "Level 1",  debug_level.LEVEL_1  },
    { 3,  "Level 2",  debug_level.LEVEL_2  },
}

cpx_proto.prefs.debug = Pref.enum("Debug", default_settings.debug_level,
                            "The debug printing level", debug_pref_enum)

cpx_proto.prefs.port  = Pref.uint("Port number", default_settings.port,
                            "The UDP port number for CPX UDP")

----------------------------------------
-- a function for handling prefs being changed
function cpx_proto.prefs_changed()
    dprint2("prefs_changed called")

    default_settings.debug_level  = cpx_proto.prefs.debug
    reset_debug_level()

    if default_settings.port ~= cpx_proto.prefs.port then
        -- remove old one, if not 0
        if default_settings.port ~= 0 then
            dprint2("removing CPX UDP from port",default_settings.port)
            DissectorTable.get("udp.port"):remove(default_settings.port, cpx_proto)
        end
        -- set our new default
        default_settings.port = cpx_proto.prefs.port
        -- add new one, if not 0
        if default_settings.port ~= 0 then
            dprint2("adding CPX UDP to port",default_settings.port)
            DissectorTable.get("udp.port"):add(default_settings.port, cpx_proto)
        end
    end

end

dprint2("CPX UDP registered")

-- this is the size of the CPX_UDP header (4 bytes)
local CPX_UDP_HDR_LEN = 4

-- this holds the Dissector object for Netlink, which we invoke in
-- our FPM dissector to dissect the encapsulated Netlink protocol
local subdiss_table = DissectorTable.new("cpx_udp.function", "CPX Function", ftypes.UINT8, base.HEX, cpx_proto)

-- this holds the plain "data" Dissector, in case we can't dissect it as Netlink
local data = Dissector.get("data")

----------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "dns.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function cpx_proto.dissector(tvbuf,pktinfo,root)
    dprint2("cpx_proto.dissector called")

    -- We want to check that the packet size is rational during dissection, so let's get the length of the
    -- packet buffer (Tvb).
    -- Because DNS has no additional payload data other than itself, and it rides on UDP without padding,
    -- we can use tvb:len() or tvb:reported_len() here; but I prefer tvb:reported_length_remaining() as it's safer.
    local offset = 0
    local length_val = tvbuf:reported_length_remaining()

    -- now let's check it's not too short
    if length_val < CPX_UDP_HDR_LEN then
        -- since we're going to add this protocol to a specific UDP port, we're going to
        -- assume packets in this port are our protocol, so the packet being too short is an error
        -- the old way: tree:add_expert_info(PI_MALFORMED, PI_ERROR, "packet too short")
        -- the correct way now:
        tree:add_proto_expert_info(ef_too_short)
        dprint("packet length ", length_val, " is too short")
        return
    end

    -- We start by adding our protocol to the dissection display tree.
    -- A call to tree:add() returns the child created, so we can add more "under" it using that return value.
    -- The second argument is how much of the buffer/packet this added tree item covers/represents - in this
    -- case (DNS protocol) that's the remainder of the packet.
    local tree = root:add(cpx_proto, tvbuf:range(0, length_val))
    
    local seq_tvbr = tvbuf:range(offset + 0, 2)
    local seq_val = seq_tvbr:le_uint()
    local seq_item = tree:add_le(header_fields.sequence, seq_tvbr)
    seq_item:append_text(string.format(' (0x%04x)', seq_val))
    
    -- dissect the CPX header field
    local cpx_header_tvbr = tvbuf:range(offset + 2, 2)
    tree:add_le(header_fields.destination, cpx_header_tvbr)
    tree:add_le(header_fields.source, cpx_header_tvbr)
    tree:add_le(header_fields.last_packet, cpx_header_tvbr)
    tree:add_le(header_fields.reserved, cpx_header_tvbr)
    tree:add_le(header_fields.function_, cpx_header_tvbr)
    tree:add_le(header_fields.version, cpx_header_tvbr)
    
    local src_label = cpx_target_valstr[source_field().value]
    local dest_label = cpx_target_valstr[destination_field().value]
    local function_val = function_field().value

    -- set the protocol column to show our protocol name
    pktinfo.cols.protocol:set("CPX UDP")

    -- set the info column
    if string.find(tostring(pktinfo.cols.info), "^CPX:") == nil then
        pktinfo.cols.info:set("")
    else
        pktinfo.cols.info:append(", ")
    end
    pktinfo.cols.info:append(string.format("CPX: %s → %s, Len=%d, Func=0x%02x", src_label, dest_label, length_val, function_val))

    dprint2("Invoking 'data' dissector")

    -- packet body
    tvbs = tvbuf(offset + CPX_UDP_HDR_LEN, length_val - CPX_UDP_HDR_LEN):tvb()
    
    local subdiss = subdiss_table:get_dissector(function_val)
    if default_settings.subdissect and subdiss ~= nil then
        dprint2("CPX trying sub-dissector for function:", default_settings.subdiss_type)

        subdiss:call(tvbs, pktinfo, root)

        dprint2("CPX finished with sub-dissector")
    else
        dprint2("CPX sub-dissection disabled or unknown CPX function, invoking 'data' dissector")
        -- append the INFO column
        if string.find(tostring(pktinfo.cols.info), "^CPX:") == nil then
            pktinfo.cols.info:append(": Unknown")
        else
            pktinfo.cols.info:append(", Unknown")
        end

        data:call(tvbs, pktinfo, root)
    end

    return pktlen
end

----------------------------------------
-- we want to have our protocol dissection invoked for a specific UDP port,
-- so get the udp dissector table and add our protocol to it
DissectorTable.get("udp.port"):add(default_settings.port, cpx_proto)

----------------------------------------
-- we also want to add the heuristic dissector, for any UDP protocol
-- first we need a heuristic dissection function
-- this is that function - when wireshark invokes this, it will pass in the same
-- things it passes in to the "dissector" function, but we only want to actually
-- dissect it if it's for us, and we need to return true if it's for us, or else false
-- figuring out if it's for us or not is not easy
-- we need to try as hard as possible, or else we'll think it's for us when it's
-- not and block other heuristic dissectors from getting their chance
--
-- in practice, you'd never set a dissector like this to be heuristic, because there
-- just isn't enough information to safely detect if it's DNS or not
-- but I'm doing it to show how it would be done
--
-- Note: this heuristic stuff is new in 1.11.3
-- local function heur_dissect_dns(tvbuf,pktinfo,root)
--     dprint2("heur_dissect_dns called")

--     -- if our preferences tell us not to do this, return false
--     if not default_settings.heur_enabled then
--         return false
--     end

--     if tvbuf:len() < DNS_HDR_LEN then
--         dprint("heur_dissect_dns: tvb shorter than DNS_HDR_LEN of:",DNS_HDR_LEN)
--         return false
--     end

--     local tvbr = tvbuf:range(0,DNS_HDR_LEN)

--     -- the first 2 bytes are transaction id, which can be anything so no point in checking those
--     -- the next 2 bytes contain flags, a couple of which have some values we can check against

--     -- the opcode has to be 0, 1, 2, 4 or 5
--     -- the opcode field starts at bit offset 17 (in C-indexing), for 4 bits in length
--     local check = tvbr:bitfield(17,4)
--     if check == 3 or check > 5 then
--         dprint("heur_dissect_dns: invalid opcode:",check)
--         return false
--     end

--     -- the rcode has to be 0-10, 16-22 (we're ignoring private use rcodes here)
--     -- the rcode field starts at bit offset 28 (in C-indexing), for 4 bits in length
--     check = tvbr:bitfield(28,4)
--     if check > 22 or (check > 10 and check < 16) then
--         dprint("heur_dissect_dns: invalid rcode:",check)
--         return false
--     end

--     dprint2("heur_dissect_dns checking questions/answers")

--     -- now let's verify the number of questions/answers are reasonable
--     check = tvbr:range(4,2):uint()  -- num questions
--     if check > 100 then return false end
--     check = tvbr:range(6,2):uint()  -- num answers
--     if check > 100 then return false end
--     check = tvbr:range(8,2):uint()  -- num authority
--     if check > 100 then return false end
--     check = tvbr:range(10,2):uint()  -- num additional
--     if check > 100 then return false end

--     dprint2("heur_dissect_dns: everything looks good calling the real dissector")

--     -- don't do this line in your script - I'm just doing it so our test-suite can
--     -- verify this script
--     root:add("Heuristic dissector used"):set_generated()

--     -- ok, looks like it's ours, so go dissect it
--     -- note: calling the dissector directly like this is new in 1.11.3
--     -- also note that calling a Dissector object, as this does, means we don't
--     -- get back the return value of the dissector function we created previously
--     -- so it might be better to just call the function directly instead of doing
--     -- this, but this script is used for testing and this tests the call() function
--     dns.dissector(tvbuf,pktinfo,root)

--     -- since this is over a transport protocol, such as UDP, we can set the
--     -- conversation to make it sticky for our dissector, so that all future
--     -- packets to/from the same address:port pair will just call our dissector
--     -- function directly instead of this heuristic function
--     -- this is a new attribute of pinfo in 1.11.3
--     pktinfo.conversation = dns

--     return true
-- end

-- now register that heuristic dissector into the udp heuristic list
-- dns:register_heuristic("udp", heur_dissect_dns)

-- We're done!
-- our protocol (Proto) gets automatically registered after this script finishes loading
----------------------------------------
