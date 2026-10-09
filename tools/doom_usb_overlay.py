"""Doom capture-only USB composite additions to the pinned board CDC overlay."""
import hashlib
import re


def capture_link(nm, text, text_vma, descriptor_source):
    symbols = {name: (int(address, 16), int(size, 16), kind) for address, size, kind, name
               in re.findall(r"^([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+([A-Za-z])\s+(\S+)$", nm, re.M)}
    if any(name in symbols for name in ("usb_g_ep_write", "usb_g_bulk_write", "usb_g_iso_write")):
        raise ValueError("USB capture link retained a polling packet writer")
    source = re.search(r"fm1_usb_audio_capture_descriptor\s*\[[^]]+\]\s*=\s*\{([^}]+)\}", descriptor_source)
    if not source:
        raise ValueError("USB capture descriptor source is missing")
    expected = bytes(int(token, 0) for token in re.findall(r"0x[0-9a-fA-F]+|\b\d+\b", source[1]))
    if len(expected) != 99:
        raise ValueError("USB capture descriptor length changed")
    def linked(name, length):
        if name not in symbols or symbols[name][1] != length:
            raise ValueError("USB capture link symbol size changed: " + name)
        offset = symbols[name][0] - text_vma
        if not 0 <= offset <= len(text) - length:
            raise ValueError("USB capture descriptor is outside XIP: " + name)
        return text[offset:offset + length]
    actual = linked("fm1_usb_audio_capture_descriptor", 99)
    if actual != expected:
        raise ValueError("USB capture linked descriptor differs from source")
    device = linked("fm1_usb_device_descriptor", 18)
    if device[4:7] != b"\xef\x02\x01":
        raise ValueError("USB capture composite identity changed")
    if "audio_dma" not in symbols:
        raise ValueError("USB capture DMA symbol is missing")
    address, size, kind = symbols["audio_dma"]
    if size != 256 or address % 64 or kind.lower() != "b" or not 0x01c00000 <= address < 0x01c80000:
        raise ValueError("USB capture DMA size/alignment/internal RAM placement changed")
    return {"capture_only": True, "rate": 44100, "bits": 16, "channels": 2,
            "interfaces": [2, 3], "endpoint": "0x81", "max_packet_bytes": 180,
            "descriptor_sha256": hashlib.sha256(actual).hexdigest(),
            "static_endpoint_dma_bytes": size, "dma_address": hex(address),
            "packet_commit": "CPU0 bounded epoch-checked attempt; SDK polling writers absent",
            "tap": "music/effects before physical master gain"}

def device(text, vendor):
    text = vendor.device(text)
    return vendor.once(text, "        cdc_register(usb_id);", """        cdc_register(usb_id);
        extern u32 fm1_uac_desc_config(usb_dev,u8 *,u32 *);
        usb_add_desc_config(usb_id,class_index++,fm1_uac_desc_config);""")

def cdc(text, vendor):
    text = '#include "fm1_usb_audio_target.h"\n' + vendor.cdc(text)
    return vendor.function(text, "cdc_write_data", """u32 fm1_cdc_write_packet(const usb_dev usb_id, u8 *buf, u32 len, unsigned generation)
{
    if(!cdc_hdl[usb_id] || !len || len>=MAXP_SIZE_CDC_BULKIN ||
       (cdc_hdl[usb_id]->bmTransceiver & (BIT(0)|BIT(4)))!=(BIT(0)|BIT(4)))return 0;
    return fm1_usb_packet_write(usb_id,CDC_DATA_EP_IN,buf,len,
                              &fm1_cdc_generation,generation);
}
u32 cdc_write_data(const usb_dev usb_id, u8 *buf, u32 len)
{
    return fm1_cdc_write_packet(usb_id,buf,len,fm1_cdc_generation);
}""")

def descriptors(text, vendor):
    text = vendor.once(text,
        "{18,1,0,2,2,2,1,64,0x54,0x36,0x55,0x51,0,2,1,2,0,1}",
        "{18,1,0,2,0xef,2,1,64,0x54,0x36,0x55,0x51,4,2,1,2,0,1}")
    return vendor.once(text, '"FM1 USB Diagnostic"', '"FM1 Doom USB Audio"')

def policy(text, vendor):
    text = vendor.once(text, "ep==0 || ep==2 || ep==3", "ep==0 || ep==1 || ep==2 || ep==3")
    text = vendor.once(text, "r->wIndex>1", "r->wIndex>3")
    return vendor.once(text, "r->wIndex!=2 && r->wIndex!=0x82 && r->wIndex!=0x83",
        "r->wIndex!=0x81 && r->wIndex!=2 && r->wIndex!=0x82 && r->wIndex!=0x83")
