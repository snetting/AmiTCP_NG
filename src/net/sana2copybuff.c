/*
 * AmiTCP_NG -- a modernised, open fork of AmiTCP/IP 3.0b2.
 * Modifications for AmiTCP_NG Copyright (C) 2026 Andy Taylor (MW0MWZ).
 * Licensed under the GNU General Public License, version 2 (see COPYING).
 * The original AmiTCP/IP and BSD copyright notices are retained below.
 */

RCS_ID_C="$Id: sana2copybuff.c,v 1.15 1993/12/20 18:06:41 jraja Exp $";
/*
 * Copyright (c) 1993 AmiTCP/IP Group, <amitcp-group@hut.fi>,
 *                    Helsinki University of Technology, Finland.
 *                    All rights reserved.
 *
 * sana2copybuff.c - Buffer Management Routines for Sana-II Interfaces
 *
 * Last modified: Sun Nov  7 01:37:49 1993 ppessi
 *
 * HISTORY
 * $Log: sana2copybuff.c,v $
 * Revision 1.15  1993/12/20  18:06:41  jraja
 * Added more robust version of ioip_alloc_mbuf().
 * Added more detail to "mbuf chain short" message.
 *
 * Revision 1.13  1993/11/06  23:39:15  ppessi
 * Added more information to "mbuf chain short" error message.
 *
 * Revision 1.12  1993/06/04  11:16:15  jraja
 * Fixes for first public release.
 *
 * Revision 1.11  1993/05/16  21:09:43  ppessi
 * RCS version changed.
 *
 * Revision 1.10  1993/05/05  16:10:38  puhuri
 * Fixed cluster allocation code.
 *
 * Revision 1.9  93/04/26  11:53:11  11:53:11  too (Tomi Ollila)
 * Changed include paths of amiga_api.h, amiga_libcallentry.h and amiga_raf.h
 * from kern to api
 * 
 * Revision 1.8  93/04/24  22:46:13  22:46:13  jraja (Jarno Tapio Rajahalme)
 * Removed Define for USECLUSTERS
 * 
 * Revision 1.7  93/04/13  22:22:56  22:22:56  jraja (Jarno Tapio Rajahalme)
 * Changed return from buffer allocation function back to recent form.
 * 
 * Revision 1.6  93/04/12  09:20:40  09:20:40  jraja (Jarno Tapio Rajahalme)
 * Changed reserved mbuf chain so that all mbufs after the header have 
 * clusters.
 * 
 * Revision 1.5  93/04/05  17:46:26  17:46:26  jraja (Jarno Tapio Rajahalme)
 * Changed spl storage variables to spl_t.
 * Changed every .c file to use conf.h.
 * 
 * Revision 1.4  93/03/20  07:11:55  07:11:55  ppessi (Pekka Pessi)
 * Fixed mbuf allocating for headers
 * 
 * Revision 1.3  93/03/05  19:51:16  19:51:16  jraja (Jarno Tapio Rajahalme)
 * Fixed includes (again).
 * 
 * Revision 1.2  93/02/28  22:21:57  22:21:57  ppessi (Pekka Pessi)
 * Made to compile; used RAFn macros.
 * 
 * Revision 1.1  93/02/25  14:34:29  14:34:29  ppessi (Pekka Pessi)
 * Initial revision
 */

/*
 * sana2copybuff.c --- the mbuf<->driver payload bridge (SANA-II callbacks).
 *
 * A SANA-II driver knows nothing about mbufs. When it needs to move packet
 * payload, it calls back into US through two hook functions we register on each
 * IORequest -- this file provides them. They are the exact seam where "the Amiga
 * driver's buffer" meets "the BSD stack's mbuf chain". docs/ARCHITECTURE.md sect 8.
 *
 *   m_copy_to_mbuf   (the S2_CopyToBuff hook)  -- RECEIVE. The driver calls this
 *                    with a just-arrived frame; we copy it INTO a fresh mbuf chain
 *                    (allocated by ioip_alloc_mbuf from the interrupt-safe pool).
 *                    Runs at DEVICE INTERRUPT time: no AllocMem, hence the pool.
 *   m_copy_from_mbuf (the S2_CopyFromBuff hook) -- TRANSMIT. The driver calls this
 *                    to pull payload OUT of our mbuf chain into its transmit buffer.
 *
 * Both are declared with register-argument calling conventions (RAF3/SAVEDS) and
 * the __saveds attribute because the DRIVER calls them from its own context, not
 * ours -- see api/apicalls_gnuc.h for the same pattern. Read ioip_alloc_mbuf()
 * first (it sizes and grabs the receive mbuf chain), then the two hooks.
 */

#include <conf.h>

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/mbuf.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/syslog.h>
#include <sys/synch.h>

#include <net/if.h>

#if INET
#include <netinet/in.h>
#include <netinet/in_systm.h>
#include <netinet/in_var.h>
#include <netinet/ip.h>
#endif

#include <net/if_sana.h>
#include <api/amiga_raf.h>

/*
 * allocate mbufs for the size MTU at free_chain for read request
 */
#if 0 /* old one */
BOOL
ioip_alloc_mbuf(struct IOIPReq *s2rp, ULONG MTU)
{
  register struct mbuf *m, *n;
  register int len = 0;

  n = s2rp->ioip_reserved;

  /* Check for packet header */
  if (n && (n->m_flags & M_PKTHDR)) {
    /* There is already a full packet  */
    return TRUE;
  }

  /* Prepend by a packet header */
  MGETHDR(m, M_NOWAIT, MT_HEADER);
  if (m) { 
    m->m_len = len = MHLEN;
    s2rp->ioip_reserved = m;
    m->m_next = n;
    
    /* Find the end of the free chain */ /* ASSUME THAT THESE HAVE CLUSTERS */
    while (n = m->m_next) {
      len += n->m_len; m = n;
    } 
    
    /*
     * add new (cluster)mbufs to get the desired size
     */
    while (len < MTU) {
      MGET(n, M_NOWAIT, MT_DATA);
      if (n != NULL) {
	MCLGET(n, M_NOWAIT);
	if (n->m_ext.ext_buf != NULL) {
	  n->m_len = n->m_ext.ext_size;
	  len += n->m_ext.ext_size;
	}
	else {
	  m_free(n);
	  break;
	}
	m = m->m_next = n;
      }
      else
	break;
    }
    
    s2rp->ioip_reserved->m_pkthdr.len = len;
  }
  if (len < MTU) { 
    m_freem(s2rp->ioip_reserved);
    s2rp->ioip_reserved = NULL;
    return FALSE;
  }

  return TRUE;
}
#else
BOOL
ioip_alloc_mbuf(struct IOIPReq *s2rp, ULONG MTU)
{
  register struct mbuf *m, *n;
  register int len;

  /*
   * s2rp->ioip_reserved is either NULL, or an mbuf chain, possibly having
   * packet header in the first one.
   */
  n = s2rp->ioip_reserved;

  /* Check for packet header */
  if (n && (n->m_flags & M_PKTHDR)) {
    /*
     * chain already has the packet header
     */
    m = n;
    len = m->m_len;
  }
  else {
    /* Prepend by a packet header */
    MGETHDR(m, M_NOWAIT, MT_HEADER);
    if (m) { 
      m->m_len = len = MHLEN;
      s2rp->ioip_reserved = m;
      m->m_next = n;
    }
    else
      goto fail;
  }
  /*
   * Now m points to the start of the mbuf chain. The first mbuf has
   * a packet header. 
   */

  /* Find the end of the free chain */ /* ASSUME THAT THESE HAVE CLUSTERS */
  while ((n = m->m_next)) {
    len += n->m_len; m = n;
  }
    
  /*
   * Now len has the total length of the mbuf chain, add new 
   * (cluster)mbufs to get the desired size (MTU).
   */
  while (len < MTU) {
    MGET(n, M_NOWAIT, MT_DATA);
    if (n != NULL) {
      MCLGET(n, M_NOWAIT);
      if (n->m_ext.ext_buf != NULL) {
	len += n->m_len = n->m_ext.ext_size;
      }
      else {
	m_free(n);
        goto fail;
      }
      m = m->m_next = n;
    }
    else
      goto fail;
  }
  s2rp->ioip_reserved->m_pkthdr.len = len;
  return TRUE;

 fail:
  m_freem(s2rp->ioip_reserved);
  s2rp->ioip_reserved = NULL;
  return FALSE;
}
#endif

/* Return one contiguous, writable mbuf area suitable for the SANA-II
 * S2_DMACopyToBuff32 callback.  The receive pool deliberately keeps the
 * packet header separate from its cluster: small packets can therefore use
 * the header mbuf without consuming a cluster. */
static APTR
ioip_dma_rx_buffer(struct IOIPReq *s2rp, ULONG length)
{
  struct mbuf *m = s2rp->ioip_reserved;
  APTR data;
  ULONG capacity;

  if (m == NULL || !(m->m_flags & M_PKTHDR) || length == 0)
    return NULL;

  if (length <= (ULONG)m->m_len) {
    data = mtod(m, APTR);
    capacity = (ULONG)m->m_len;
  } else {
    m = m->m_next;
    if (m == NULL || !(m->m_flags & M_EXT))
      return NULL;
    data = mtod(m, APTR);
    capacity = (ULONG)m->m_len;
  }

  /* SANA-II requires a contiguous, 32-bit aligned buffer whose capacity is
   * a multiple of four bytes.  Keep these checks runtime-visible: mbuf pool
   * sizing is configurable and should not silently invalidate DMA. */
  if (capacity < length || (capacity & 3) != 0 ||
      ((ULONG)data & 3) != 0)
    return NULL;

  s2rp->ioip_dma_rx = m;
  s2rp->ioip_dma_rx_capacity = capacity;
  s2rp->ioip_rx_state = IOIP_RX_DMA_OFFERED;
  return data;
}

/* S2_DMACopyToBuff32: the driver may DMA the received packet directly into
 * one of our reserved mbufs.  This runs at device interrupt time. */
static SAVEDS APTR RAF1(m_dma_copy_to_mbuf,
			 struct IOIPReq*, req, a0)
#if 0
{
#endif
  if (req == NULL)
    return NULL;

  if (req->ioip_rx_state == IOIP_RX_DMA_OFFERED &&
      req->ioip_dma_rx != NULL)
    return mtod(req->ioip_dma_rx, APTR);

  if (req->ioip_rx_state != IOIP_RX_POSTED)
    return NULL;

  return ioip_dma_rx_buffer(req, req->ioip_s2.ios2_DataLength);
}

/* Finalize a successful direct receive after the device has filled the
 * offered mbuf.  The byte-copy callback performs this same ownership move
 * inside its interrupt-time callback, but DMA cannot do so until completion. */
BOOL
ioip_finalize_dma_rx(struct IOIPReq *s2rp)
{
  struct mbuf *reserved, *m, *tail;
  ULONG length;

  if (s2rp == NULL || s2rp->ioip_rx_state != IOIP_RX_DMA_OFFERED ||
      (m = s2rp->ioip_dma_rx) == NULL)
    return FALSE;

  length = s2rp->ioip_s2.ios2_DataLength;
  if (length > s2rp->ioip_dma_rx_capacity)
    goto bad;

  reserved = s2rp->ioip_reserved;
  if (reserved == NULL || !(reserved->m_flags & M_PKTHDR))
    goto bad;

  if (m == reserved) {
    tail = m->m_next;
    m->m_next = NULL;
    m->m_len = length;
    m->m_pkthdr.len = length;
    s2rp->ioip_reserved = tail;
  } else if (m == reserved->m_next) {
    tail = m->m_next;
    reserved->m_next = tail;
    m->m_next = NULL;
    m->m_flags |= M_PKTHDR;
    m->m_len = length;
    m->m_pkthdr.len = length;
  } else {
    goto bad;
  }

  s2rp->ioip_packet = m;
  s2rp->ioip_dma_rx = NULL;
  s2rp->ioip_dma_rx_capacity = 0;
  s2rp->ioip_rx_state = IOIP_RX_POSTED;
  s2rp->ioip_if->ss_dmain++;
  return TRUE;

bad:
  s2rp->ioip_dma_rx = NULL;
  s2rp->ioip_dma_rx_capacity = 0;
  s2rp->ioip_rx_state = IOIP_RX_POSTED;
  return FALSE;
}

/*
 * Copy data from an mbuf chain starting from the beginning,
 * continuing for "n" bytes, into the indicated continuous buffer.
 *
 * NOTE: this WILL be called from INTERRUPTS, so compile with stack checking
 *       disabled and use __saveds if near data is needed.
 */
static SAVEDS BOOL RAF3(m_copy_from_mbuf,
		 BYTE*,          to,   a0,
		 struct IOIPReq*,from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  register struct mbuf *m = from->ioip_packet;
  register unsigned count;

  from->ioip_if->ss_copyout++;		/* SANA2CopyStats: byte CopyFromBuff (TX) */

  while (n > 0) {
    /*
     * PORT (AmiTCP_NG) security fix: this guard MUST be unconditional. `n` is
     * the length the driver asked us to copy; if it exceeds the mbuf chain we
     * hold, m walks off the end to NULL and the bcopy below dereferences it --
     * on this MMU-less 68000 that reads through address 0, i.e. the Exec vector
     * table. The original compiled the only check out unless DIAGNOSTIC was set.
     * Keep the detailed log under DIAGNOSTIC, but always bail.
     */
    if (m == 0) {
#if DIAGNOSTIC
      log(LOG_ERR, "m_copy_from_buff: mbuf chain short");
#endif
      return FALSE;
    }
    count = MIN(m->m_len, n);
    bcopy(mtod(m, caddr_t), to, count);
    n -= count;
    to += count;
    m = m->m_next;
  }
  return TRUE;
}

/*
 * Copy data from an continuous buffer 'from' to preallocated mbuf chain
 * starting from the beginning, continuing for "n" bytes.
 * Mbufs in the preallocated chain must have their m_len field set to maximum
 * amount of data that they can have.
 * 
 * NOTE: this WILL be called from INTERRUPTS, so compile with stack checking
 *       disabled and use __saveds if near data is needed.
 */
static SAVEDS BOOL RAF3(m_copy_to_mbuf,
		 struct IOIPReq*,to,   a0,
		 BYTE*,          from, a1,
		 ULONG,          n,    d0)
#if 0
{
#endif
  register struct mbuf *f, *m = to->ioip_reserved;
  unsigned totlen = n;

  /* A DMA-capable driver may probe first and then fall back to this callback.
   * The copy path owns packet finalization in that case. */
  to->ioip_dma_rx = NULL;
  to->ioip_dma_rx_capacity = 0;
  to->ioip_rx_state = IOIP_RX_COPIED;
  to->ioip_if->ss_copyin++;		/* SANA2CopyStats: byte CopyToBuff (RX) */

#if DIAGNOSTIC
  if (!(m->m_flags & M_PKTHDR)) {
    log(LOG_ERR, "m_copy_to_buff: mbuf chain has no header");
    return FALSE;
  }
#endif

  while (n > 0) {
    /*
     * PORT (AmiTCP_NG) security fix: as in m_copy_from_mbuf, this guard MUST be
     * unconditional. A driver over-reporting the frame length walks m off the
     * end of the preallocated chain to NULL; the bcopy below would then write
     * wire data through address 0 (the Exec vectors on this no-MMU 68000). Keep
     * the diagnostic detail under DIAGNOSTIC, but always refuse to deref NULL.
     */
    if (m == 0) {
#if DIAGNOSTIC
      log(LOG_ERR, "m_copy_to_buff: mbuf chain short, "
	  "packet len =%lu, reserved =%lu, "
	  "wiretype =%lu, mtu =%lu",
	  totlen, to->ioip_reserved->m_pkthdr.len,
	  to->ioip_s2.ios2_PacketType,
	  (ULONG)to->ioip_if->ss_if.if_mtu);
#endif
      return FALSE;
    }
    if (n < m->m_len)
      m->m_len = n;
    bcopy(from, mtod(m, caddr_t), m->m_len);
    from += m->m_len;
    n -= m->m_len;
    if (n > 0)
      m = m->m_next;
  }

  /*
   * move the packet to the field 'ioip_packet',
   * set total length of the packet and terminate it.
   */
  f = m->m_next;		/* first free mbuf */
  m->m_next = NULL;		/* terminate the chain */

  to->ioip_packet = to->ioip_reserved;
  to->ioip_packet->m_pkthdr.len = totlen; /* set packet length */
  to->ioip_reserved = f;		/* leftover mbufs */

  /*
   * More mbuf flags and interface pointer must be set later
   */
  return TRUE;
}

struct TagItem buffermanagement_legacy[3] = {
    { S2_CopyToBuff,   (ULONG)m_copy_to_mbuf },
    { S2_CopyFromBuff, (ULONG)m_copy_from_mbuf },
    { TAG_END, }
};

struct TagItem buffermanagement[4] = {
    { S2_CopyToBuff,       (ULONG)m_copy_to_mbuf },
    { S2_CopyFromBuff,     (ULONG)m_copy_from_mbuf },
    { S2_DMACopyToBuff32,  (ULONG)m_dma_copy_to_mbuf },
    { TAG_END, }
};
