-------------------------------------------------------------------------------
--
-- author: Elia Cereda <eliacereda@gmail.com>
-- Copyright (c) 2023, Elia Cereda
-- All rights reserved
--
-- Version: 1.0
--
-------------------------------------------------------------------------------
--[[

    This code is a plugin for Wireshark, to dissect Quagga FPM Netlink
    protocol messages over TCP.

    The purpose of this script is two-fold:
        1) To decode a protocol Wireshark does not (currently) decode natively.
        2) To provide a tutorial for TCP-based Lua dissection.

    Because of the second goal (a tutorial), this script has a lot more comments
    than one would normally expect or want.

----------------------------------------

    OVERVIEW:

    This Lua plugin script dissects Quagga/zebra-style FPM messages carrying
    Netlink messages, over TCP connections.

    Wireshark has a "Netlink" protocol dissector, but it currently expects
    to be running on a Linux cooked-mode SLL header and link type. That's
    because Netlink has traditionally been used between the Linux kernel
    and user-space apps. But the open-source Quagga, zebra, and the
    commercial ZebOS routing products also send Netlink messages over TCP
    to other processes or even outside the box, to a "Forwarding Plane Manager"
    (FPM) that controls forwarding-plane devices (typically hardware).

    The Netlink message is encapsulated within an FPM header, which identifies
    an FPM message version (currently 1), the type of message it contains
    (namely a Netlink message), and its length.

    So we have:
    struct fpm_msg_hdr_t
    {
        uint8_t  version;
        uint8_t  msg_type;
        uint16_t msg_len;
    }
    followed by a Netlink message.

    Note that there is no Linux cooked-mode SLL header in this case.
    Therefore, to be able to re-use Wireshark's built-in Netlink dissector,
    this Lua script creates a fake SLL header, and invokes the built-in
    Netlink dissector using that.


----------------------------------------

    HOW TO RUN THIS SCRIPT:
    
    Wireshark and Tshark support multiple ways of loading Lua scripts: through
    a dofile() call in init.lua, through the file being in either the global
    or personal plugins directories, or via the command line.

    See the Wireshark Developer's Guide chapter on Lua
    (https://www.wireshark.org/docs/wsdg_html_chunked/wsluarm_modules.html).
    Once this script is loaded, it creates a new protocol named "FPM", as
    described in the "Background" section. If you have a capture file with FPM
    messages in it, simply select one in the Packet List pane, right-click on
    it, and select "Decode As ...", and then in the dialog box that shows up
    scroll down the list of protocols to one called "FPM", select that and
    click the "ok" or "apply" button. Voila`, you're now decoding FPM packets
    using the dissector in this script. Another way is to download the capture
    file made for this script (called "segmented_fpm.pcap", and open that -
    since the FPM packets in it use TCP port 2620, and since the FPM protocol
    in this script has been set to automatically decode TCP port 2620, it will
    automagically do it without doing "Decode As ...".

----------------------------------------

    Writing Lua Dissectors for TCP-based Protocols:

    A Lua-based protocol dissector for TCP works much the same as one for UDP,
    which is described in the 'dissector.lua' tutorial script available on
    https://wiki.wireshark.org/Lua/Examples. The main differences are
    interfacing with Wireshark's Lua API for reassembly, and handling the
    various conditions that can arise due to running on TCP.

    In particular, your dissector function needs to handle the following
    conditions which can occur for TCP-based packet captures:
        1) The TCP packet segment might only have a portion of your message.
        2) The TCP packet segment might contain multiple of your messages.
        3) The TCP packet might be in the middle of your message, because
           a previous segment was not captured.
        4) The TCP packet might be cut-off, because the user set Wireshark to
           limit the size of the packets being captured.
        5) Any combination of the above.

    For case (4), the simplest thing to do is just not dissect packets that
    are cut-off. Check the Tvb's len() vs. reported_len(), and if they're
    different that means the packet was cut-off.

    For case (3), your dissector should try to perform some sanity checking of
    an early field if possible. If the sanity check fails, then ignore this
    packet and wait for the next one. "Ignoring" the packet means returning
    the number 0 from your dissector.

    For case (2), currently this requires you to write your own while-loop,
    dissecting your message within the while-loop, such that you can dissect
    multiple of your messages each time Wireshark invokes your Proto's
    dissector() function.

    For case (1), you have to dissect your message enough to figure out what
    the full length will be - if you can figure that out, then set the Pinfo's
    desegment_len to how many more bytes than are currently in the Tvb that
    you need in order to decode the full message; or if you don't know exactly
    how many more bytes you need, then set the Pinfo desegment_len to the pre-
    defined value of "DESEGMENT_ONE_MORE_SEGMENT". You also need to set the
    Pinfo's desegment_offset to the offset in the tvbuff at which you want the
    dissector to continue processing when next invoked by Wireshark. The next
    time a TCP packet segment is received by Wireshark, it will invoke your
    Proto's dissector function with a Tvb buffer composed of the data bytes
    starting at the desegment_offset of the previous Tvb buffer together with
    desegment_len more bytes.

    For the return value of your Proto's dissector() function, you should
    return one of the following:
        1) If the packet does not belong to your dissector, return 0. You must
           *not* set the Pinfo.desegment_len nor the desegment_offset if you
           return 0.
        2) If you need more bytes, set the Pinfo's
           desegment_len/desegment_offset as described earlier, and return
           either nothing, or return the length of the Tvb. Either way is fine.
        3) If you don't need more bytes, return either nothing, or return the
           length of the Tvb. Either way is fine.

    See the code in this script for an example of the above.

]]----------------------------------------

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
    port         = 5000, -- default TCP port number for CPX
    max_msg_len  = 4092, -- max length of CPX message
    subdissect   = true, -- whether to call sub-dissector or not
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
local cpx_proto = Proto("cpx", "CPX")


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

----------------------------------------
-- a table of all of our Protocol's fields
local header_fields = {
    length      = ProtoField.uint16("cpx_tcp.length", "Length", base.UNIT_STRING, {" byte", " bytes"}),

    destination = ProtoField.uint16("cpx.destination", "Destination", base.HEX, cpx_target_valstr, 0x0007),
    source      = ProtoField.uint16("cpx.source", "Source", base.HEX, cpx_target_valstr, 0x0038),
    last_packet = ProtoField.uint16("cpx.last_packet", "Last packet", base.DEC, cpx_last_packet_valstr, 0x0040),
    reserved    = ProtoField.uint16("cpx.reserved", "Reserved", base.DEC, {}, 0x0080),
    function_   = ProtoField.uint16("cpx.function", "Function", base.HEX, cpx_function_valstr, 0x3F00),
    version     = ProtoField.uint16("cpx.version", "Version", base.DEC, cpx_version_valstr, 0xC000),
}

-- register the ProtoFields
cpx_proto.fields = header_fields

local destination_field = Field.new('cpx.destination')
local source_field = Field.new('cpx.source')
local function_field = Field.new('cpx.function')

dprint2("cpx_proto ProtoFields registered")

-- due to a bug in older (prior to 1.12) wireshark versions, we need to keep newly created
-- Tvb's for longer than the duration of the dissect function (see bug 10888)
-- this bug only affects dissectors that create new Tvb's, which is not that common
-- but this FPM dissector happens to do it in order to create the fake SLL header
-- to pass on to the Netlink dissector
local tvbs = {}

---------------------------------------
-- This function will be invoked by Wireshark during initialization, such as
-- at program start and loading a new file
function cpx_proto.init()
    -- reset the save Tvbs
    tvbs = {}
end

-- this is the size of the CPX_TCP header (4 bytes)
local CPX_TCP_HDR_LEN = 4

-- some forward "declarations" of helper functions we use in the dissector
-- local createSllTvb, dissect_cpx, checkFpmLength
local dissect_cpx, check_cpx_length

-- this holds the Dissector object for Netlink, which we invoke in
-- our FPM dissector to dissect the encapsulated Netlink protocol
local subdiss_table = DissectorTable.new("cpx.function", "CPX Function", ftypes.UINT8, base.HEX, cpx_proto)

-- this holds the plain "data" Dissector, in case we can't dissect it as Netlink
local data = Dissector.get("data")

--------------------------------------------------------------------------------
-- The following creates the callback function for the dissector.
-- It's the same as doing "cpx_proto.dissector = function (tvbuf,pkt,root)"
-- The 'tvbuf' is a Tvb object, 'pktinfo' is a Pinfo object, and 'root' is a TreeItem object.
-- Whenever Wireshark dissects a packet that our Proto is hooked into, it will call
-- this function and pass it these arguments for the packet it's dissecting.
function cpx_proto.dissector(tvbuf, pktinfo, root)
    dprint2("cpx_proto.dissector called")
    -- reset the save Tvbs
    tvbs = {}

    -- get the length of the packet buffer (Tvb).
    local pktlen = tvbuf:len()

    local bytes_consumed = 0

    -- we do this in a while loop, because there could be multiple CPX messages
    -- inside a single TCP segment, and thus in the same tvbuf - but our
    -- cpx_proto.dissector() will only be called once per TCP segment, so we
    -- need to do this loop to dissect each CPX message in it
    while bytes_consumed < pktlen do

        -- We're going to call our "dissect()" function, which is defined
        -- later in this script file. The dissect() function returns the
        -- length of the CPX message it dissected as a positive number, or if
        -- it's a negative number then it's the number of additional bytes it
        -- needs if the Tvb doesn't have them all. If it returns a 0, it's a
        -- dissection error.
        local result = dissect_cpx(tvbuf, pktinfo, root, bytes_consumed)

        if result > 0 then
            -- we successfully processed an CPX message, of 'result' length
            bytes_consumed = bytes_consumed + result
            -- go again on another while loop
        elseif result == 0 then
            -- If the result is 0, then it means we hit an error of some kind,
            -- so return 0. Returning 0 tells Wireshark this packet is not for
            -- us, and it will try heuristic dissectors or the plain "data"
            -- one, which is what should happen in this case.
            return 0
        else
            -- we need more bytes, so set the desegment_offset to what we
            -- already consumed, and the desegment_len to how many more
            -- are needed
            pktinfo.desegment_offset = bytes_consumed

            -- invert the negative result so it's a positive number
            result = -result

            pktinfo.desegment_len = result

            -- even though we need more bytes, this packet is for us, so we
            -- tell wireshark all of its bytes are for us by returning the
            -- number of Tvb bytes we "successfully processed", namely the
            -- length of the Tvb
            return pktlen
        end        
    end

    -- In a TCP dissector, you can either return nothing, or return the number of
    -- bytes of the tvbuf that belong to this protocol, which is what we do here.
    -- Do NOT return the number 0, or else Wireshark will interpret that to mean
    -- this packet did not belong to your protocol, and will try to dissect it
    -- with other protocol dissectors (such as heuristic ones)
    return bytes_consumed
end

----------------------------------------
-- The following is a local function used for dissecting our FPM messages
-- inside the TCP segment using the desegment_offset/desegment_len method.
-- It's a separate function because we run over TCP and thus might need to
-- parse multiple messages in a single segment/packet. So we invoke this
-- function only dissects one FPM message and we invoke it in a while loop
-- from the Proto's main disector function.
--
-- This function is passed in the original Tvb, Pinfo, and TreeItem from the Proto's
-- dissector function, as well as the offset in the Tvb that this function should
-- start dissecting from.
--
-- This function returns the length of the FPM message it dissected as a
-- positive number, or as a negative number the number of additional bytes it
-- needs if the Tvb doesn't have them all, or a 0 for error.
--
dissect_cpx = function (tvbuf, pktinfo, root, offset)
    dprint2("CPX dissect function called")

    local length_val, length_tvbr = check_cpx_length(tvbuf, offset)

    if length_val <= 0 then
        return length_val
    end

    -- if we got here, then we have a whole message in the Tvb buffer
    -- so let's finish dissecting it...

    -- We start by adding our protocol to the dissection display tree.
    local tree = root:add(cpx_proto, tvbuf:range(offset, length_val))
    
    -- dissect the length field
    local length_item = tree:add_le(header_fields.length, length_tvbr, length_val)
    length_item:append_text(string.format(' (0x%04x)', length_tvbr:le_uint()))
    
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
    pktinfo.cols.protocol:set("CPX TCP")
    
    -- set the info column
    if string.find(tostring(pktinfo.cols.info), "^CPX:") == nil then
        pktinfo.cols.info:set("")
    else
        pktinfo.cols.info:append(", ")
    end
    pktinfo.cols.info:append(string.format("CPX: %s → %s, Len=%d, Func=0x%02x", src_label, dest_label, length_val, function_val))

    dprint2("Invoking 'data' dissector")

    -- packet body
    tvbs[#tvbs+1] = tvbuf(offset + CPX_TCP_HDR_LEN, length_val - CPX_TCP_HDR_LEN):tvb()
    
    local subdiss = subdiss_table:get_dissector(function_val)
    if default_settings.subdissect and subdiss ~= nil then
        dprint2("CPX trying sub-dissector for function:", default_settings.subdiss_type)

        subdiss:call(tvbs[#tvbs], pktinfo, root)

        dprint2("CPX finished with sub-dissector")
    else
        dprint2("CPX sub-dissection disabled or unknown CPX function, invoking 'data' dissector")
        -- append the INFO column
        if string.find(tostring(pktinfo.cols.info), "^CPX:") == nil then
            pktinfo.cols.info:append(": Unknown")
        else
            pktinfo.cols.info:append(", Unknown")
        end

        data:call(tvbs[#tvbs], pktinfo, root)
    end
    
    return length_val
end


----------------------------------------
-- The function to check the length field.
--
-- This returns two things: (1) the length, and (2) the TvbRange object, which
-- might be nil if length <= 0.
check_cpx_length = function (tvbuf, offset)

    -- "msglen" is the number of bytes remaining in the Tvb buffer which we
    -- have available to dissect in this run
    local msglen = tvbuf:len() - offset

    -- check if capture was only capturing partial packet size
    if msglen ~= tvbuf:reported_length_remaining(offset) then
        -- captured packets are being sliced/cut-off, so don't try to desegment/reassemble
        dprint2("Captured packet was shorter than original, can't reassemble")
        return 0
    end

    if msglen < CPX_TCP_HDR_LEN then
        -- we need more bytes, so tell the main dissector function that we
        -- didn't dissect anything, and we need an unknown number of more
        -- bytes (which is what "DESEGMENT_ONE_MORE_SEGMENT" is used for)
        dprint2("Need more bytes to figure out CPX length field")
        -- return as a negative number
        return -DESEGMENT_ONE_MORE_SEGMENT
    end

    -- if we got here, then we know we have enough bytes in the Tvb buffer
    -- to at least figure out the full length of this CPX messsage (the length
    -- is the 16-bit integer in first and second bytes)

    -- get the TvbRange of bytes 1+2
    local length_tvbr = tvbuf:range(offset, 2)

    -- get the length as a little-endian unsigned integer
    local length_val  = length_tvbr:le_uint() + CPX_TCP_HDR_LEN

    if length_val > default_settings.max_msg_len then
        -- too many bytes, invalid message
        dprint("CPX message length is too long: ", length_val)
        return 0
    end

    if msglen < length_val then
        -- we need more bytes to get the whole CPX message
        dprint2("Need more bytes to desegment full CPX")
        return -(length_val - msglen)
    end

    return length_val, length_tvbr
end

--------------------------------------------------------------------------------
-- We want to have our protocol dissection invoked for a specific TCP port,
-- so get the TCP dissector table and add our protocol to it.
local function enableDissector()
    -- using DissectorTable:set() removes existing dissector(s), whereas the
    -- DissectorTable:add() one adds ours before any existing ones, but
    -- leaves the other ones alone, which is better
    DissectorTable.get("tcp.port"):add(default_settings.port, cpx_proto)
end
-- call it now, because we're enabled by default
enableDissector()

local function disableDissector()
    DissectorTable.get("tcp.port"):remove(default_settings.port, cpx_proto)
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
cpx_proto.prefs.enabled     = Pref.bool("Dissector enabled", default_settings.enabled,
                                        "Whether the CPX dissector is enabled or not")

cpx_proto.prefs.subdissect  = Pref.bool("Enable sub-dissectors", default_settings.subdissect,
                                        "Whether the CPX packet's content" ..
                                        " should be dissected or not")

cpx_proto.prefs.debug       = Pref.enum("Debug", default_settings.debug_level,
                                        "The debug printing level", debug_pref_enum)

----------------------------------------
-- the function for handling preferences being changed
function cpx_proto.prefs_changed()
    dprint2("prefs_changed called")

    default_settings.subdissect  = cpx_proto.prefs.subdissect

    default_settings.debug_level = cpx_proto.prefs.debug
    resetDebugLevel()

    if default_settings.enabled ~= cpx_proto.prefs.enabled then
        default_settings.enabled = cpx_proto.prefs.enabled
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
