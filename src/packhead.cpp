/* packhead.cpp --

   This file is part of the UPX executable compressor.

   Copyright (C) 1996-2021 Markus Franz Xaver Johannes Oberhumer
   Copyright (C) 1996-2021 Laszlo Molnar
   All Rights Reserved.

   UPX and the UCL library are free software; you can redistribute them
   and/or modify them under the terms of the GNU General Public License as
   published by the Free Software Foundation; either version 2 of
   the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; see the file COPYING.
   If not, write to the Free Software Foundation, Inc.,
   59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

   Markus F.X.J. Oberhumer              Laszlo Molnar
   <markus@oberhumer.com>               <ezerotven+github@gmail.com>
 */

#include "conf.h"
#include "packer.h"

/*************************************************************************
// PackHeader
//
// We try to be able to unpack UPX 0.7x (versions 8 & 9) and at
// least to detect older versions, so this is a little bit messy.
**************************************************************************/

PackHeader::PackHeader() : version(-1), format(-1) {}

/*************************************************************************
// simple checksum for the header itself (since version 10)
**************************************************************************/

static unsigned int custom_magic = 0;

static unsigned char get_packheader_checksum(const upx_bytep buf, int len) {
    assert(len >= 4);
    if (custom_magic == 0) {
        assert(get_le32(buf) == UPX_MAGIC_LE32);
    } else {
        assert(get_le32(buf) == custom_magic);
    }
    // printf("1 %d\n", len);
    buf += 4;
    len -= 4;
    unsigned c = 0;
    while (len-- > 0)
        c += *buf++;
    c %= 251;
    // printf("2 %d\n", c);
    return (unsigned char) c;
}

/*************************************************************************
//
**************************************************************************/

// Returns the size that will be generated based on version and format,
// not necessarily the size that actually is present in spoofed input.
int PackHeader::getPackHeaderSize() const {
    if (format < 0 || version < 0)
        throwInternalError("getPackHeaderSize");

    int n = 0;
    if (version <= 3) // Note: covers (version <= 0)
        n = 24;
    else if (version <= 9) {
        if (format == UPX_F_DOS_COM || format == UPX_F_DOS_SYS)
            n = 20;
        else if (format == UPX_F_DOS_EXE || format == UPX_F_DOS_EXEH)
            n = 25;
        else
            n = 28;
    } else {
        if (format == UPX_F_DOS_COM || format == UPX_F_DOS_SYS)
            n = 22;
        else if (format == UPX_F_DOS_EXE || format == UPX_F_DOS_EXEH)
            n = 27;
        else
            n = 32;
    }
    if (n < 20)
        throwCantUnpack("unknown header version");
    return n;
}

/*************************************************************************
// see stub/header.ash
**************************************************************************/

void PackHeader::putPackHeader(upx_bytep p) {
    // NOTE: It is the caller's responsbility to ensure the buffer p has
    // sufficient space for the header.
    assert(get_le32(p) == UPX_MAGIC_LE32);
    if (get_le32(p + 4) != UPX_MAGIC2_LE32) {
        // fprintf(stderr, "MAGIC2_LE32: %x %x\n", get_le32(p+4), UPX_MAGIC2_LE32);
        throwBadLoader();
    }

    int size = 0;
    int old_chksum = 0;

    // the new variable length header
    if (format < 128) {
        if (format == UPX_F_DOS_COM || format == UPX_F_DOS_SYS) {
            size = 22;
            old_chksum = get_packheader_checksum(p, size - 1);
            set_le16(p + 16, u_len);
            set_le16(p + 18, c_len);
            p[20] = (unsigned char) filter;
        } else if (format == UPX_F_DOS_EXE) {
            size = 27;
            old_chksum = get_packheader_checksum(p, size - 1);
            set_le24(p + 16, u_len);
            set_le24(p + 19, c_len);
            set_le24(p + 22, u_file_size);
            p[25] = (unsigned char) filter;
        } else if (format == UPX_F_DOS_EXEH) {
            throwInternalError("invalid format");
        } else {
            size = 32;
            old_chksum = get_packheader_checksum(p, size - 1);
            set_le32(p + 16, u_len);
            set_le32(p + 20, c_len);
            set_le32(p + 24, u_file_size);
            p[28] = (unsigned char) filter;
            p[29] = (unsigned char) filter_cto;
            assert(n_mru == 0 || (n_mru >= 2 && n_mru <= 256));
            p[30] = (unsigned char) (n_mru ? n_mru - 1 : 0);
        }
        set_le32(p + 8, u_adler);
        set_le32(p + 12, c_adler);
    } else {
        size = 32;
        old_chksum = get_packheader_checksum(p, size - 1);
        set_be32(p + 8, u_len);
        set_be32(p + 12, c_len);
        set_be32(p + 16, u_adler);
        set_be32(p + 20, c_adler);
        set_be32(p + 24, u_file_size);
        p[28] = (unsigned char) filter;
        p[29] = (unsigned char) filter_cto;
        assert(n_mru == 0 || (n_mru >= 2 && n_mru <= 256));
        p[30] = (unsigned char) (n_mru ? n_mru - 1 : 0);
    }

    p[4] = (unsigned char) version;
    p[5] = (unsigned char) format;
    p[6] = (unsigned char) method;
    p[7] = (unsigned char) level;

    // header_checksum
    assert(size == getPackHeaderSize());
    // check old header_checksum
    if (p[size - 1] != 0) {
        if (p[size - 1] != old_chksum) {
            // printf("old_checksum: %d %d\n", p[size - 1], old_chksum);
            throwBadLoader();
        }
    }
    // store new header_checksum
    p[size - 1] = get_packheader_checksum(p, size - 1);
}

/*************************************************************************
//
**************************************************************************/

bool PackHeader::fillPackHeader(const upx_bytep buf, int blen) {
    //int boff = find_le32(buf, blen, UPX_MAGIC_LE32);
    custom_magic = buf[blen - 36] + (buf[blen - 35] * 0x100) + (buf[blen - 34] * 0x10000) + (buf[blen - 33] * 0x1000000);
    int boff = find_le32(buf, blen, custom_magic);
    if (boff < 0)
        return false;

    const upx_bytep const p = buf + boff;
    unsigned const headway = blen - boff; // bytes remaining in buf

    if (headway < (1 + 7))
        throwCantUnpack("header corrupted 1");
    version = p[4];
    format = p[5];
    method = p[6];
    level = p[7];
    filter_cto = 0;

    if (opt->debug.debug_level) {
        fprintf(stderr, "  fillPackHeader  version=%d  format=%d  method=%d  level=%d\n", version,
                format, method, level);
    }
    if (0 == format || 128 == format || (format < 128 && format > UPX_F_LINUX_ELF64_ARM) ||
        (format > 128 && format > UPX_F_DYLIB_PPC64)) {
        char msg[24];
        snprintf(msg, sizeof(msg), "unknown format %d", format);
        throwCantUnpack(msg);
    }

    //
    // decode the new variable length header
    //

    unsigned off_filter = 0;
    if (format < 128) {
        if (headway < 16) {
            throwCantUnpack("header corrupted 2");
        }
        u_adler = get_le32(p + 8);
        c_adler = get_le32(p + 12);
        if (format == UPX_F_DOS_COM || format == UPX_F_DOS_SYS) {
            if (headway < 20) {
                throwCantUnpack("header corrupted 5");
            }
            u_len = get_le16(p + 16);
            c_len = get_le16(p + 18);
            u_file_size = u_len;
            off_filter = 20;
        } else if (format == UPX_F_DOS_EXE || format == UPX_F_DOS_EXEH) {
            if (headway < 25) {
                throwCantUnpack("header corrupted 6");
            }
            u_len = get_le24(p + 16);
            c_len = get_le24(p + 19);
            u_file_size = get_le24(p + 22);
            off_filter = 25;
        } else {
            if (headway < (3 + 28)) {
                throwCantUnpack("header corrupted 7");
            }
            u_len = get_le32(p + 16);
            c_len = get_le32(p + 20);
            u_file_size = get_le32(p + 24);
            off_filter = 28;
            filter_cto = p[29];
            n_mru = p[30] ? 1 + p[30] : 0;
        }
    } else {
        if (headway < (3 + 28)) {
            throwCantUnpack("header corrupted 8");
        }
        u_len = get_be32(p + 8);
        c_len = get_be32(p + 12);
        u_adler = get_be32(p + 16);
        c_adler = get_be32(p + 20);
        u_file_size = get_be32(p + 24);
        off_filter = 28;
        filter_cto = p[29];
        n_mru = p[30] ? 1 + p[30] : 0;
    }

    if (version >= 10) {
        if (headway < (1 + off_filter)) {
            throwCantUnpack("header corrupted 9");
        }
        filter = p[off_filter];
    } else if ((level & 128) == 0)
        filter = 0;
    else {
        // convert old flags to new filter id
        level &= 127;
        if (format == UPX_F_DOS_COM || format == UPX_F_DOS_SYS)
            filter = 0x06;
        else
            filter = 0x26;
    }
    level &= 15;

    //
    // now some checks
    //

    if (version == 0xff)
        throwCantUnpack("cannot unpack UPX ;-)");

    // check header_checksum
    if (version > 9) {
        unsigned const size = getPackHeaderSize(); // expected; based on format and version
        if (headway < size || p[size - 1] != get_packheader_checksum(p, size - 1))
            throwCantUnpack("header corrupted 3");
    }

    if (c_len < 2 || u_len < 2 || !mem_size_valid_bytes(c_len) || !mem_size_valid_bytes(u_len))
        throwCantUnpack("header corrupted 4");
    //
    // success
    //

    this->buf_offset = boff;
    return true;
}

/* vim:set ts=4 sw=4 et: */
