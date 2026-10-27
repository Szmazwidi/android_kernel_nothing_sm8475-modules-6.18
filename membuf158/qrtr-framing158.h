/* SPDX-License-Identifier: GPL-2.0-only */
/* QRTR v1/v3 framing from redducc6.6 af_qrtr.c, commit db9180c.
 * Original framing implementation: Sony Mobile Communications Inc.2015,
 * The Linux Foundation2013. Local adaptation: bounded unaligned wire reads,
 * reject overflow/oversized frames before passing to native6.18 QRTR.
 */
static unsigned pkt158_le32(const unsigned char *p) {
 return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;
}
static int qrtr_peek_pkt_size(const void *data) {
 const unsigned char *p=data;unsigned header,size;
 if(p[0]==1){header=32;size=pkt158_le32(p+20);}
 else if(p[0]==3){header=16+p[3];size=pkt158_le32(p+4);}
 else return -22;
 if(header>MAX_PKT_SZ||size>MAX_PKT_SZ-header)return -22;
 size=(size+3U)&~3U;
 if(size>MAX_PKT_SZ-header)return -22;
 return size+header;
}
