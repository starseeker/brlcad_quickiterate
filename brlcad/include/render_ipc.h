/*                    R E N D E R _ I P C . H
 * BRL-CAD
 *
 * Copyright (c) 2025 United States Government as represented by
 * the U.S. Army Research Laboratory.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * version 2.1 as published by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this file; see the file named COPYING for more
 * information.
 */
/** @addtogroup librender */
/** @{ */
/** @file render_ipc.h
 *
 * @brief
 *  Wire-protocol definitions for the rt_ipc IPC channel.
 *
 * rt_ipc communicates with its parent process via a libuv anonymous
 * pipe (UNIX domain socket / named pipe on Windows).  This header
 * defines the byte-level packet format used over that pipe.
 *
 * All multi-byte integers are stored in little-endian order.
 * The header is packed (no padding) so that sizeof() is reliable.
 *
 * ### Packet flow
 *
 *   parent -> rt_ipc :  RENDER_IPC_T_JOB    (one per render)
 *   parent -> rt_ipc :  RENDER_IPC_T_CANCEL (optional, at any time)
 *
 *   rt_ipc -> parent :  RENDER_IPC_T_PIXELS (one per scanline)
 *   rt_ipc -> parent :  RENDER_IPC_T_PROGRESS (periodic)
 *   rt_ipc -> parent :  RENDER_IPC_T_DONE   (render finished)
 *   rt_ipc -> parent :  RENDER_IPC_T_ERROR  (failure)
 *
 * ### Frame format
 *
 *   [magic:4] [type:1] [paylen:4] [payload:paylen]
 *
 *   magic  = RENDER_IPC_MAGIC (0x52544950, ASCII "RTIP")
 *   type   = one of RENDER_IPC_T_*
 *   paylen = payload length in bytes (LE uint32)
 *
 */

#ifndef RENDER_IPC_H
#define RENDER_IPC_H

#include "common.h"
#include <stdint.h>

__BEGIN_DECLS

/* ------------------------------------------------------------------ */
/* Magic value and protocol version                                     */
/* ------------------------------------------------------------------ */

/** @brief 4-byte magic that starts every packet (ASCII "RTIP"). */
#define RENDER_IPC_MAGIC    0x52544950u

/** @brief Current protocol version stored in render_ipc_job.version. */
#define RENDER_IPC_VERSION  1u


/* ------------------------------------------------------------------ */
/* Packet type identifiers                                              */
/* ------------------------------------------------------------------ */

/** @brief Job submission: parent → rt_ipc. */
#define RENDER_IPC_T_JOB      0x01u

/** @brief Scanline pixel data: rt_ipc → parent. */
#define RENDER_IPC_T_PIXELS   0x02u

/** @brief Render complete (success): rt_ipc → parent. */
#define RENDER_IPC_T_DONE     0x03u

/** @brief Render failed: rt_ipc → parent. */
#define RENDER_IPC_T_ERROR    0x04u

/** @brief Cancel request: parent → rt_ipc. */
#define RENDER_IPC_T_CANCEL   0x05u

/** @brief Progress update: rt_ipc → parent. */
#define RENDER_IPC_T_PROGRESS 0x06u


/* ------------------------------------------------------------------ */
/* Packet header (9 bytes, packed)                                      */
/* ------------------------------------------------------------------ */

#pragma pack(push, 1)

/**
 * @brief Fixed-size header that precedes every packet payload.
 *
 * sizeof(struct render_ipc_hdr) == 9 bytes when packed.
 */
struct render_ipc_hdr {
    uint32_t magic;   /**< @brief Always RENDER_IPC_MAGIC. */
    uint8_t  type;    /**< @brief Packet type (RENDER_IPC_T_*). */
    uint32_t paylen;  /**< @brief Payload length in bytes (may be 0). */
};


/* ------------------------------------------------------------------ */
/* Job payload (follows header for RENDER_IPC_T_JOB)                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Fixed portion of the job request payload.
 *
 * Immediately after this structure, the payload contains:
 *   - The NUL-terminated database file path (@c dbfile_len bytes incl. NUL)
 *   - @c nobjs NUL-terminated object-name strings
 *
 * Total payload length:
 *   sizeof(struct render_ipc_job) + dbfile_len + sum(strlen(obj_i) + 1)
 */
struct render_ipc_job {
    uint32_t version;       /**< @brief Protocol version (RENDER_IPC_VERSION). */
    int32_t  width;         /**< @brief Image width in pixels. */
    int32_t  height;        /**< @brief Image height in pixels. */
    int32_t  lightmodel;    /**< @brief Lighting model (RENDER_LIGHT_*). */
    int32_t  nthreads;      /**< @brief Worker threads (0 = all CPUs). */
    int32_t  nobjs;         /**< @brief Number of object names that follow. */
    double   view2model[16];/**< @brief 4x4 view-to-model matrix (row-major). */
    double   eye[3];        /**< @brief Eye position in model coordinates (mm). */
    double   viewsize;      /**< @brief View diameter in model units (mm). */
    double   aspect;        /**< @brief Pixel aspect ratio (width/height). */
    double   background[3]; /**< @brief Background RGB, 0.0–1.0 each channel. */
    uint32_t dbfile_len;    /**< @brief Bytes for dbfile string incl. NUL. */
};


/* ------------------------------------------------------------------ */
/* Pixel scanline payload (follows header for RENDER_IPC_T_PIXELS)     */
/* ------------------------------------------------------------------ */

/**
 * @brief Fixed portion of the pixel scanline payload.
 *
 * Immediately after this structure, the payload contains:
 *   @c count * 3 bytes of packed RGB pixel data.
 *
 * Total payload length:  sizeof(struct render_ipc_pixels) + count * 3
 */
struct render_ipc_pixels {
    int32_t x;     /**< @brief X of leftmost pixel in this scanline (≥ 0). */
    int32_t y;     /**< @brief Y of this scanline (0 = bottom row). */
    int32_t count; /**< @brief Number of pixels (== image width for full rows). */
};


/* ------------------------------------------------------------------ */
/* Progress payload (follows header for RENDER_IPC_T_PROGRESS)         */
/* ------------------------------------------------------------------ */

/**
 * @brief Payload for periodic progress reports.
 */
struct render_ipc_progress {
    int32_t done;  /**< @brief Scanlines completed so far. */
    int32_t total; /**< @brief Total scanlines in this render. */
};


/* ------------------------------------------------------------------ */
/* Error payload (follows header for RENDER_IPC_T_ERROR)               */
/* ------------------------------------------------------------------ */

/**
 * @brief Error report payload.
 *
 * Followed by a NUL-terminated human-readable error message.
 */
struct render_ipc_error {
    int32_t  code;    /**< @brief Error code (errno or application-defined). */
    uint32_t msglen;  /**< @brief Bytes in the following NUL-terminated string. */
};

#pragma pack(pop)

__END_DECLS

#endif /* RENDER_IPC_H */

/** @} */
/*
 * Local Variables:
 * mode: C
 * tab-width: 8
 * indent-tabs-mode: t
 * c-file-style: "stroustrup"
 * End:
 * ex: shiftwidth=4 tabstop=8
 */
