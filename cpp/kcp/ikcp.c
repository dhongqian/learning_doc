//=====================================================================
//
// KCP - A Better ARQ Protocol Implementation
// skywind3000 (at) gmail.com, 2010-2011
//  
// Features:
// + Average RTT reduce 30% - 40% vs traditional ARQ like tcp.
// + Maximum RTT reduce three times vs tcp.
// + Lightweight, distributed as a single source file.
//
//=====================================================================
#include "ikcp.h"

#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdio.h>



//=====================================================================
// KCP BASIC
//=====================================================================
const IUINT32 IKCP_RTO_NDL = 30;		// no delay min rto
const IUINT32 IKCP_RTO_MIN = 100;		// normal min rto
const IUINT32 IKCP_RTO_DEF = 200;
const IUINT32 IKCP_RTO_MAX = 60000;
const IUINT32 IKCP_CMD_PUSH = 81;		// cmd: push data
const IUINT32 IKCP_CMD_ACK  = 82;		// cmd: ack
const IUINT32 IKCP_CMD_WASK = 83;		// cmd: window probe (ask)
const IUINT32 IKCP_CMD_WINS = 84;		// cmd: window size (tell)
const IUINT32 IKCP_ASK_SEND = 1;		// need to send IKCP_CMD_WASK
const IUINT32 IKCP_ASK_TELL = 2;		// need to send IKCP_CMD_WINS
const IUINT32 IKCP_WND_SND = 32;
const IUINT32 IKCP_WND_RCV = 128;       // must >= max fragment size
const IUINT32 IKCP_MTU_DEF = 1400;
const IUINT32 IKCP_ACK_FAST	= 3;
const IUINT32 IKCP_INTERVAL	= 100;
const IUINT32 IKCP_OVERHEAD = 24;
const IUINT32 IKCP_DEADLINK = 20;
const IUINT32 IKCP_THRESH_INIT = 2;
const IUINT32 IKCP_THRESH_MIN = 2;
const IUINT32 IKCP_PROBE_INIT = 7000;		// 7 secs to probe window size
const IUINT32 IKCP_PROBE_LIMIT = 120000;	// up to 120 secs to probe window


//---------------------------------------------------------------------
// encode / decode
//---------------------------------------------------------------------

/* encode 8 bits unsigned int */
static inline char *ikcp_encode8u(char *p, unsigned char c)
{
	*(unsigned char*)p++ = c;
	return p;
}

/* decode 8 bits unsigned int */
static inline const char *ikcp_decode8u(const char *p, unsigned char *c)
{
	*c = *(unsigned char*)p++;
	return p;
}

/* encode 16 bits unsigned int (lsb) */
static inline char *ikcp_encode16u(char *p, unsigned short w)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (w & 255);
	*(unsigned char*)(p + 1) = (w >> 8);
#else
	*(unsigned short*)(p) = w;
#endif
	p += 2;
	return p;
}

/* decode 16 bits unsigned int (lsb) */
static inline const char *ikcp_decode16u(const char *p, unsigned short *w)
{
#if IWORDS_BIG_ENDIAN
	*w = *(const unsigned char*)(p + 1);
	*w = *(const unsigned char*)(p + 0) + (*w << 8);
#else
	*w = *(const unsigned short*)p;
#endif
	p += 2;
	return p;
}

/* encode 32 bits unsigned int (lsb) */
static inline char *ikcp_encode32u(char *p, IUINT32 l)
{
#if IWORDS_BIG_ENDIAN
	*(unsigned char*)(p + 0) = (unsigned char)((l >>  0) & 0xff);
	*(unsigned char*)(p + 1) = (unsigned char)((l >>  8) & 0xff);
	*(unsigned char*)(p + 2) = (unsigned char)((l >> 16) & 0xff);
	*(unsigned char*)(p + 3) = (unsigned char)((l >> 24) & 0xff);
#else
	*(IUINT32*)p = l;
#endif
	p += 4;
	return p;
}

/* decode 32 bits unsigned int (lsb) */
static inline const char *ikcp_decode32u(const char *p, IUINT32 *l)
{
#if IWORDS_BIG_ENDIAN
	*l = *(const unsigned char*)(p + 3);
	*l = *(const unsigned char*)(p + 2) + (*l << 8);
	*l = *(const unsigned char*)(p + 1) + (*l << 8);
	*l = *(const unsigned char*)(p + 0) + (*l << 8);
#else 
	*l = *(const IUINT32*)p;
#endif
	p += 4;
	return p;
}

static inline IUINT32 _imin_(IUINT32 a, IUINT32 b) {
	return a <= b ? a : b;
}

static inline IUINT32 _imax_(IUINT32 a, IUINT32 b) {
	return a >= b ? a : b;
}

static inline IUINT32 _ibound_(IUINT32 lower, IUINT32 middle, IUINT32 upper) 
{
	return _imin_(_imax_(lower, middle), upper);
}

static inline long _itimediff(IUINT32 later, IUINT32 earlier) 
{
	return ((IINT32)(later - earlier));
}

//---------------------------------------------------------------------
// manage segment
//---------------------------------------------------------------------
typedef struct IKCPSEG IKCPSEG;

static void* (*ikcp_malloc_hook)(size_t) = NULL;
static void (*ikcp_free_hook)(void *) = NULL;

// internal malloc
static void* ikcp_malloc(size_t size) {
	if (ikcp_malloc_hook) 
		return ikcp_malloc_hook(size);
	return malloc(size);
}

// internal free
static void ikcp_free(void *ptr) {
	if (ikcp_free_hook) {
		ikcp_free_hook(ptr);
	}	else {
		free(ptr);
	}
}

// redefine allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*))
{
	ikcp_malloc_hook = new_malloc;
	ikcp_free_hook = new_free;
}

// allocate a new kcp segment
static IKCPSEG* ikcp_segment_new(ikcpcb *kcp, int size)
{
	return (IKCPSEG*)ikcp_malloc(sizeof(IKCPSEG) + size);
}

// delete a segment
static void ikcp_segment_delete(ikcpcb *kcp, IKCPSEG *seg)
{
	ikcp_free(seg);
}

// write log
void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...)
{
	char buffer[1024];
	va_list argptr;
	if ((mask & kcp->logmask) == 0 || kcp->writelog == 0) return;
	va_start(argptr, fmt);
	vsprintf(buffer, fmt, argptr);
	va_end(argptr);
	kcp->writelog(buffer, kcp, kcp->user); 
}

// check log mask
static int ikcp_canlog(const ikcpcb *kcp, int mask)
{
	if ((mask & kcp->logmask) == 0 || kcp->writelog == NULL) return 0;
	return 1;
}

// output segment
static int ikcp_output(ikcpcb *kcp, const void *data, int size)
{
	assert(kcp);
	assert(kcp->output);
	if (ikcp_canlog(kcp, IKCP_LOG_OUTPUT)) {
		ikcp_log(kcp, IKCP_LOG_OUTPUT, "[RO] %ld bytes", (long)size);
	}
	if (size == 0) return 0;
	return kcp->output((const char*)data, size, kcp, kcp->user);
}

// output queue
void ikcp_qprint(const char *name, const struct IQUEUEHEAD *head)
{
#if 0
	const struct IQUEUEHEAD *p;
	printf("<%s>: [", name);
	for (p = head->next; p != head; p = p->next) {
		const IKCPSEG *seg = iqueue_entry(p, const IKCPSEG, node);
		printf("(%lu %d)", (unsigned long)seg->sn, (int)(seg->ts % 10000));
		if (p->next != head) printf(",");
	}
	printf("]\n");
#endif
}


//---------------------------------------------------------------------
// create a new kcpcb
//---------------------------------------------------------------------
ikcpcb* ikcp_create(IUINT32 conv, void *user)
{
	ikcpcb *kcp = (ikcpcb*)ikcp_malloc(sizeof(struct IKCPCB));
	if (kcp == NULL) return NULL;
	kcp->conv = conv;
	kcp->user = user;
	kcp->snd_una = 0;
	kcp->snd_nxt = 0;
	kcp->rcv_nxt = 0;
	kcp->ts_recent = 0;
	kcp->ts_lastack = 0;
	kcp->ts_probe = 0;
	kcp->probe_wait = 0;
	kcp->snd_wnd = IKCP_WND_SND;
	kcp->rcv_wnd = IKCP_WND_RCV;
	kcp->rmt_wnd = IKCP_WND_RCV;
	kcp->cwnd = 0;
	kcp->incr = 0;
	kcp->probe = 0;
	kcp->mtu = IKCP_MTU_DEF;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;
	kcp->stream = 0;

    /// buffer用来组装ACK包，默认大小是(1400 + 24)*3
	kcp->buffer = (char*)ikcp_malloc((kcp->mtu + IKCP_OVERHEAD) * 3);
	if (kcp->buffer == NULL) {
		ikcp_free(kcp);
		return NULL;
	}

	iqueue_init(&kcp->snd_queue);
	iqueue_init(&kcp->rcv_queue);
	iqueue_init(&kcp->snd_buf);
	iqueue_init(&kcp->rcv_buf);
	kcp->nrcv_buf = 0;
	kcp->nsnd_buf = 0;
	kcp->nrcv_que = 0;
	kcp->nsnd_que = 0;
	kcp->state = 0;
	kcp->acklist = NULL;
	kcp->ackblock = 0;
	kcp->ackcount = 0;
	kcp->rx_srtt = 0;
	kcp->rx_rttval = 0;
	kcp->rx_rto = IKCP_RTO_DEF;   ///< 200
	kcp->rx_minrto = IKCP_RTO_MIN;   ///< 100
	kcp->current = 0;
	kcp->interval = IKCP_INTERVAL;   ///< 100
	kcp->ts_flush = IKCP_INTERVAL;   ///< 100
	kcp->nodelay = 0;
	kcp->updated = 0;
	kcp->logmask = 0;
	kcp->ssthresh = IKCP_THRESH_INIT;  ///< 2
	kcp->fastresend = 0;
	kcp->nocwnd = 0;
	kcp->xmit = 0;
	kcp->dead_link = IKCP_DEADLINK;   ///< 20
	kcp->output = NULL;
	kcp->writelog = NULL;

	return kcp;
}


//---------------------------------------------------------------------
// release a new kcpcb
//---------------------------------------------------------------------
void ikcp_release(ikcpcb *kcp)
{
	assert(kcp);
	if (kcp) {
		IKCPSEG *seg;
		while (!iqueue_is_empty(&kcp->snd_buf)) {
			seg = iqueue_entry(kcp->snd_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_buf)) {
			seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->snd_queue)) {
			seg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		while (!iqueue_is_empty(&kcp->rcv_queue)) {
			seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
		}
		if (kcp->buffer) {
			ikcp_free(kcp->buffer);
		}
		if (kcp->acklist) {
			ikcp_free(kcp->acklist);
		}

		kcp->nrcv_buf = 0;
		kcp->nsnd_buf = 0;
		kcp->nrcv_que = 0;
		kcp->nsnd_que = 0;
		kcp->ackcount = 0;
		kcp->buffer = NULL;
		kcp->acklist = NULL;
		ikcp_free(kcp);
	}
}


//---------------------------------------------------------------------
// set output callback, which will be invoked by kcp
//---------------------------------------------------------------------
void ikcp_setoutput(ikcpcb *kcp, int (*output)(const char *buf, int len,
	ikcpcb *kcp, void *user))
{
	kcp->output = output;
}


//---------------------------------------------------------------------
// user/upper level recv: returns size, returns below zero for EAGAIN
//---------------------------------------------------------------------
/**
 * kcp将接收到的kcp数据包还原成之前kcp发送的buffer数据
 */
int ikcp_recv(ikcpcb *kcp, char *buffer, int len)
{
	struct IQUEUEHEAD *p;
	int ispeek = (len < 0)? 1 : 0;
	int peeksize;
	int recover = 0;
	IKCPSEG *seg;
	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue))
		return -1;

	if (len < 0) len = -len;

	peeksize = ikcp_peeksize(kcp);

	if (peeksize < 0) 
		return -2;

	if (peeksize > len) 
		return -3;

	/// 接收窗口不够大
	if (kcp->nrcv_que >= kcp->rcv_wnd)
		recover = 1;

	// merge fragment
	for (len = 0, p = kcp->rcv_queue.next; p != &kcp->rcv_queue; ) {
		int fragment;
		seg = iqueue_entry(p, IKCPSEG, node);
		p = p->next;

		if (buffer) {
			memcpy(buffer, seg->data, seg->len);
			buffer += seg->len;
		}

		len += seg->len;
		fragment = seg->frg;

		if (ikcp_canlog(kcp, IKCP_LOG_RECV)) {
			ikcp_log(kcp, IKCP_LOG_RECV, "recv sn=%lu", seg->sn);
		}

		if (ispeek == 0) {
			iqueue_del(&seg->node);
			ikcp_segment_delete(kcp, seg);
			kcp->nrcv_que--;
		}

		/// 在发送的时候，有可能分片，在接收时，确保收到的，是一个完整的数据包
		if (fragment == 0) 
			break;
	}

	assert(len == peeksize);

	// move available data from rcv_buf -> rcv_queue
	while (! iqueue_is_empty(&kcp->rcv_buf)) {
		IKCPSEG *seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}	else {
			break;
		}
	}

	// fast recover
	if (kcp->nrcv_que < kcp->rcv_wnd && recover) {
		// ready to send back IKCP_CMD_WINS in ikcp_flush
		// tell remote my window size
		kcp->probe |= IKCP_ASK_TELL;
	}

	return len;
}


//---------------------------------------------------------------------
// peek data size
//---------------------------------------------------------------------
int ikcp_peeksize(const ikcpcb *kcp)
{
	struct IQUEUEHEAD *p;
	IKCPSEG *seg;
	int length = 0;

	assert(kcp);

	if (iqueue_is_empty(&kcp->rcv_queue)) return -1;

	seg = iqueue_entry(kcp->rcv_queue.next, IKCPSEG, node);

	/// 如果没有分包，直接返回包的长度
	if (seg->frg == 0) return seg->len;

	/// 如果包还没有收完，直接返回
	if (kcp->nrcv_que < seg->frg + 1) return -1;

    /// 所有分包已经接收完毕，开始统计完整包的大小
	for (p = kcp->rcv_queue.next; p != &kcp->rcv_queue; p = p->next) {
		seg = iqueue_entry(p, IKCPSEG, node);
		length += seg->len;

		/// 统计完成
		if (seg->frg == 0) break;
	}

	return length;
}


//---------------------------------------------------------------------
// user/upper level send, returns below zero for error
//---------------------------------------------------------------------
int ikcp_send(ikcpcb *kcp, const char *buffer, int len)
{
	IKCPSEG *seg;
	int count, i;

	assert(kcp->mss > 0);
	if (len < 0) return -1;

	// append to previous segment in streaming mode (if possible)
	// 先将发送队列中，最后一个还没有达到MSS大小的包，补充至MSS大小
	if (kcp->stream != 0) {
		if (!iqueue_is_empty(&kcp->snd_queue)) {
			/// 取最后一个segment
			IKCPSEG *old = iqueue_entry(kcp->snd_queue.prev, IKCPSEG, node);
			/// 每次发送的包，都填充满一个mss
			if (old->len < kcp->mss) {
				int capacity = kcp->mss - old->len;
				int extend = (len < capacity)? len : capacity;
				seg = ikcp_segment_new(kcp, old->len + extend);
				assert(seg);
				if (seg == NULL) {
					return -2;
				}
				iqueue_add_tail(&seg->node, &kcp->snd_queue);
				memcpy(seg->data, old->data, old->len);
				if (buffer) {
					memcpy(seg->data + old->len, buffer, extend);
					buffer += extend;
				}
				seg->len = old->len + extend;
				seg->frg = 0;
				len -= extend;
				iqueue_del_init(&old->node);
				ikcp_segment_delete(kcp, old);
			}
		}
		if (len <= 0) {
			return 0;
		}
	}

	/// 对剩余的数据，按mss大小，进行组包
	if (len <= (int)kcp->mss) count = 1;
	/// 如果剩余的数据，大于一个mss值，要进行分片
	else count = (len + kcp->mss - 1) / kcp->mss;

    /// 一次要发送包的最大值，不能大于或等于128个mss
	if (count >= IKCP_WND_RCV) return -2;

	if (count == 0) count = 1;

	// fragment
	/// 开始分包
	for (i = 0; i < count; i++) {
		int size = len > (int)kcp->mss ? (int)kcp->mss : len;
		seg = ikcp_segment_new(kcp, size);
		assert(seg);
		if (seg == NULL) {
			return -2;
		}
		if (buffer && len > 0) {
			memcpy(seg->data, buffer, size);
		}
		seg->len = size;
		seg->frg = (kcp->stream == 0)? (count - i - 1) : 0;
		iqueue_init(&seg->node);
		/// 每次都把要发送的包，添加到snd_queue的末尾
		iqueue_add_tail(&seg->node, &kcp->snd_queue);
		kcp->nsnd_que++;
		if (buffer) {
			buffer += size;
		}
		len -= size;
	}

	return 0;
}


//---------------------------------------------------------------------
// parse ack
//---------------------------------------------------------------------
static void ikcp_update_ack(ikcpcb *kcp, IINT32 rtt)
{
	IINT32 rto = 0;
	if (kcp->rx_srtt == 0) {
		kcp->rx_srtt = rtt;
		kcp->rx_rttval = rtt / 2;
	}	else {
		long delta = rtt - kcp->rx_srtt;
		if (delta < 0) delta = -delta;
		kcp->rx_rttval = (3 * kcp->rx_rttval + delta) / 4;
		kcp->rx_srtt = (7 * kcp->rx_srtt + rtt) / 8;
		if (kcp->rx_srtt < 1) kcp->rx_srtt = 1;
	}

	/// rto有三种常用的算法：1、经典算法  2、Karn / Partridge 算法  3、Jacobson / Karels 算法
	/// rto经典算法：1、SRTT = ( α * SRTT ) + ((1- α) * RTT)  2、RTO = min [ UBOUND,  max [ LBOUND,   (β * SRTT) ]  ] 
	/// Karn / Partridge 算法: 在经典算法的基础上，忽略重传，不把重传的RTT做采样
	/// Jacobson / Karels 算法:1、SRTT = SRTT + α (RTT – SRTT) 2、DevRTT = (1-β)*DevRTT + β*(|RTT-SRTT|)  3、RTO= µ * SRTT + ∂ *DevRTT 
	rto = kcp->rx_srtt + _imax_(kcp->interval, 4 * kcp->rx_rttval);
	kcp->rx_rto = _ibound_(kcp->rx_minrto, rto, IKCP_RTO_MAX);
}

static void ikcp_shrink_buf(ikcpcb *kcp)
{
	struct IQUEUEHEAD *p = kcp->snd_buf.next;
	if (p != &kcp->snd_buf) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		kcp->snd_una = seg->sn;
	}	else {
		kcp->snd_una = kcp->snd_nxt;
	}
}

static void ikcp_parse_ack(ikcpcb *kcp, IUINT32 sn)
{
	struct IQUEUEHEAD *p, *next;

	if (_itimediff(sn, kcp->snd_una) < 0 || _itimediff(sn, kcp->snd_nxt) >= 0)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		/// 发送成功，从缓存中删除
		if (sn == seg->sn) {
			iqueue_del(p);
			ikcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
			break;
		}
		if (_itimediff(sn, seg->sn) < 0) {
			break;
		}
	}
}

static void ikcp_parse_una(ikcpcb *kcp, IUINT32 una)
{
	struct IQUEUEHEAD *p, *next;
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		/// 当前的una已经大于seg->sn,说明seg->sn已经收到ACK了，即已经发送成功
		if (_itimediff(una, seg->sn) > 0) {
			iqueue_del(p);
			ikcp_segment_delete(kcp, seg);
			kcp->nsnd_buf--;
		}	else {
			break;
		}
	}
}

static void ikcp_parse_fastack(ikcpcb *kcp, IUINT32 sn)
{
	struct IQUEUEHEAD *p, *next;

	if (_itimediff(sn, kcp->snd_una) < 0 || _itimediff(sn, kcp->snd_nxt) >= 0)
		return;

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = next) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		next = p->next;
		if (_itimediff(sn, seg->sn) < 0) {
			break;
		}
		/// 说明sn包的ack没有收到，但sn后面的包，已经收到ACK了，即sn包有可能丢了
		else if (sn != seg->sn) {
			seg->fastack++;
		}
	}
}


//---------------------------------------------------------------------
// ack append
//---------------------------------------------------------------------
static void ikcp_ack_push(ikcpcb *kcp, IUINT32 sn, IUINT32 ts)
{
	size_t newsize = kcp->ackcount + 1;
	IUINT32 *ptr;

	if (newsize > kcp->ackblock) {
		IUINT32 *acklist;
		size_t newblock;

		for (newblock = 8; newblock < newsize; newblock <<= 1);
		acklist = (IUINT32*)ikcp_malloc(newblock * sizeof(IUINT32) * 2);

		if (acklist == NULL) {
			assert(acklist != NULL);
			abort();
		}

		if (kcp->acklist != NULL) {
			size_t x;
			for (x = 0; x < kcp->ackcount; x++) {
				acklist[x * 2 + 0] = kcp->acklist[x * 2 + 0];
				acklist[x * 2 + 1] = kcp->acklist[x * 2 + 1];
			}
			ikcp_free(kcp->acklist);
		}

		kcp->acklist = acklist;
		kcp->ackblock = newblock;
	}

	ptr = &kcp->acklist[kcp->ackcount * 2];
	ptr[0] = sn;
	ptr[1] = ts;
	kcp->ackcount++;
}

static void ikcp_ack_get(const ikcpcb *kcp, int p, IUINT32 *sn, IUINT32 *ts)
{
	if (sn) sn[0] = kcp->acklist[p * 2 + 0];
	if (ts) ts[0] = kcp->acklist[p * 2 + 1];
}


//---------------------------------------------------------------------
// parse data
//---------------------------------------------------------------------
void ikcp_parse_data(ikcpcb *kcp, IKCPSEG *newseg)
{
	struct IQUEUEHEAD *p, *prev;
	IUINT32 sn = newseg->sn;
	int repeat = 0;
	
	if (_itimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) >= 0 ||
		_itimediff(sn, kcp->rcv_nxt) < 0) {
		ikcp_segment_delete(kcp, newseg);
		return;
	}

	for (p = kcp->rcv_buf.prev; p != &kcp->rcv_buf; p = prev) {
		IKCPSEG *seg = iqueue_entry(p, IKCPSEG, node);
		prev = p->prev;
		if (seg->sn == sn) {
			repeat = 1;
			break;
		}
		if (_itimediff(sn, seg->sn) > 0) {
			break;
		}
	}

	if (repeat == 0) {
		iqueue_init(&newseg->node);
		iqueue_add(&newseg->node, p);
		kcp->nrcv_buf++;
	}	else {
		ikcp_segment_delete(kcp, newseg);
	}

#if 0
	ikcp_qprint("rcvbuf", &kcp->rcv_buf);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif

	// move available data from rcv_buf -> rcv_queue
	while (! iqueue_is_empty(&kcp->rcv_buf)) {
		IKCPSEG *seg = iqueue_entry(kcp->rcv_buf.next, IKCPSEG, node);
		if (seg->sn == kcp->rcv_nxt && kcp->nrcv_que < kcp->rcv_wnd) {
			iqueue_del(&seg->node);
			kcp->nrcv_buf--;
			iqueue_add_tail(&seg->node, &kcp->rcv_queue);
			kcp->nrcv_que++;
			kcp->rcv_nxt++;
		}	else {
			break;
		}
	}

#if 0
	ikcp_qprint("queue", &kcp->rcv_queue);
	printf("rcv_nxt=%lu\n", kcp->rcv_nxt);
#endif

#if 1
//	printf("snd(buf=%d, queue=%d)\n", kcp->nsnd_buf, kcp->nsnd_que);
//	printf("rcv(buf=%d, queue=%d)\n", kcp->nrcv_buf, kcp->nrcv_que);
#endif
}


//---------------------------------------------------------------------
// input data
//---------------------------------------------------------------------
/**
 * KCP报文分为ACK报文、数据报文、探测窗口报文、响应窗口报文四种。
kcp报文的una字段（snd_una：第一个未确认的包）表示对端希望接收的下一个kcp包序号，
也就是说明接收端已经收到了所有小于una序号的kcp包。解析una字段后需要把发送缓冲区
里面包序号小于una的包全部丢弃掉。

ack报文则包含了对端收到的kcp包的序号，接到ack包后需要删除发送缓冲区中与ack包中的
发送包序号（sn）相同的kcp包。
 */
int ikcp_input(ikcpcb *kcp, const char *data, long size)
{
	IUINT32 una = kcp->snd_una;
	IUINT32 maxack = 0;
	int flag = 0;

	if (ikcp_canlog(kcp, IKCP_LOG_INPUT)) {
		ikcp_log(kcp, IKCP_LOG_INPUT, "[RI] %d bytes", size);
	}

	/// 数据为空，或数据大小小于kcp的包头大小，报错
	if (data == NULL || (int)size < (int)IKCP_OVERHEAD) return -1;

	while (1) {
		IUINT32 ts, sn, len, una, conv;
		IUINT16 wnd;
		IUINT8 cmd, frg;
		IKCPSEG *seg;

		if (size < (int)IKCP_OVERHEAD) break;

		data = ikcp_decode32u(data, &conv);
		if (conv != kcp->conv) return -1;

		data = ikcp_decode8u(data, &cmd);
		data = ikcp_decode8u(data, &frg);
		data = ikcp_decode16u(data, &wnd);
		data = ikcp_decode32u(data, &ts);
		data = ikcp_decode32u(data, &sn);
		data = ikcp_decode32u(data, &una);
		data = ikcp_decode32u(data, &len);

		size -= IKCP_OVERHEAD;

		/// 有效数据大小不与预期不匹配，报错
		if ((long)size < (long)len || (int)len < 0) return -2;

		/// 包头解析错误，报错
		if (cmd != IKCP_CMD_PUSH && cmd != IKCP_CMD_ACK &&
			cmd != IKCP_CMD_WASK && cmd != IKCP_CMD_WINS) 
			return -3;

		kcp->rmt_wnd = wnd;
		/// 将发送缓存里，对方已收到的包删除
		ikcp_parse_una(kcp, una);
		/// 更新snd_una,即第一个未确认的包
		ikcp_shrink_buf(kcp);

		if (cmd == IKCP_CMD_ACK) {
			if (_itimediff(kcp->current, ts) >= 0) {
				/// 更新rtt和rto
				ikcp_update_ack(kcp, _itimediff(kcp->current, ts));
			}
			ikcp_parse_ack(kcp, sn);
			ikcp_shrink_buf(kcp);
			if (flag == 0) {
				flag = 1;
				maxack = sn;
			}	else {
				if (_itimediff(sn, maxack) > 0) {
					maxack = sn;
				}
			}
			if (ikcp_canlog(kcp, IKCP_LOG_IN_ACK)) {
				ikcp_log(kcp, IKCP_LOG_IN_DATA, 
					"input ack: sn=%lu rtt=%ld rto=%ld", sn, 
					(long)_itimediff(kcp->current, ts),
					(long)kcp->rx_rto);
			}
		} ///< IKCP_CMD_ACK
		/**
		 * 收到数据报文时，需要判断数据报文是否在接收窗口内，如果是则保存ack，
		 * 如果数据报文的sn正好是待接收的第一个报文rcv_nxt，那么就更新rcv_nxt(加1)。
		 * 如果配置了ackNodelay模式（无延迟ack）或者远端窗口为0（代表暂时不能发送用
		 * 户数据），那么这里会立刻fulsh（）发送ack。
		 */
		else if (cmd == IKCP_CMD_PUSH) {
			if (ikcp_canlog(kcp, IKCP_LOG_IN_DATA)) {
				ikcp_log(kcp, IKCP_LOG_IN_DATA, 
					"input psh: sn=%lu ts=%lu", sn, ts);
			}
			/// 接收窗口有空间
			if (_itimediff(sn, kcp->rcv_nxt + kcp->rcv_wnd) < 0) {
				ikcp_ack_push(kcp, sn, ts);
				if (_itimediff(sn, kcp->rcv_nxt) >= 0) {
					seg = ikcp_segment_new(kcp, len);
					seg->conv = conv;
					seg->cmd = cmd;
					seg->frg = frg;
					seg->wnd = wnd;
					seg->ts = ts;
					seg->sn = sn;
					seg->una = una;
					seg->len = len;

					if (len > 0) {
						memcpy(seg->data, data, len);
					}

					ikcp_parse_data(kcp, seg);
				}
			}
		}
		else if (cmd == IKCP_CMD_WASK) {
			// ready to send back IKCP_CMD_WINS in ikcp_flush
			// tell remote my window size
			kcp->probe |= IKCP_ASK_TELL;
			if (ikcp_canlog(kcp, IKCP_LOG_IN_PROBE)) {
				ikcp_log(kcp, IKCP_LOG_IN_PROBE, "input probe");
			}
		}
		else if (cmd == IKCP_CMD_WINS) {
			// do nothing
			if (ikcp_canlog(kcp, IKCP_LOG_IN_WINS)) {
				ikcp_log(kcp, IKCP_LOG_IN_WINS,
					"input wins: %lu", (IUINT32)(wnd));
			}
		}
		else {
			return -3;
		}

		data += len;
		size -= len;
	}

	if (flag != 0) {
		ikcp_parse_fastack(kcp, maxack);
	}

	/// 如果这一次第一个未确认的包的序号 > 上一次未第一个未确认的包
	/// 说明收到有效的包了
	if (_itimediff(kcp->snd_una, una) > 0) {
		/// 拥塞窗口大小 < 远端接收窗口大小
		if (kcp->cwnd < kcp->rmt_wnd) {
			IUINT32 mss = kcp->mss;
			/// 拥塞窗口大小 < 拥塞窗口阀值
			if (kcp->cwnd < kcp->ssthresh) {
				/// 拥塞窗口+1
				kcp->cwnd++;
				/// 可发送最大数据量增加最大分片个大小
				kcp->incr += mss;
			}	else {
				if (kcp->incr < mss) kcp->incr = mss;
				kcp->incr += (mss * mss) / kcp->incr + (mss / 16);
				if ((kcp->cwnd + 1) * mss <= kcp->incr) {
					kcp->cwnd++;
				}
			}
			if (kcp->cwnd > kcp->rmt_wnd) {
				kcp->cwnd = kcp->rmt_wnd;
				kcp->incr = kcp->rmt_wnd * mss;
			}
		}
	}

	return 0;
}


//---------------------------------------------------------------------
// ikcp_encode_seg
//---------------------------------------------------------------------
static char *ikcp_encode_seg(char *ptr, const IKCPSEG *seg)
{
	ptr = ikcp_encode32u(ptr, seg->conv);
	ptr = ikcp_encode8u(ptr, (IUINT8)seg->cmd);
	ptr = ikcp_encode8u(ptr, (IUINT8)seg->frg);
	ptr = ikcp_encode16u(ptr, (IUINT16)seg->wnd);
	ptr = ikcp_encode32u(ptr, seg->ts);
	ptr = ikcp_encode32u(ptr, seg->sn);
	ptr = ikcp_encode32u(ptr, seg->una);
	ptr = ikcp_encode32u(ptr, seg->len);
	return ptr;
}

static int ikcp_wnd_unused(const ikcpcb *kcp)
{
	if (kcp->nrcv_que < kcp->rcv_wnd) {
		return kcp->rcv_wnd - kcp->nrcv_que;
	}
	return 0;
}

void ikcp_send_ack(ikcpcb *kcp, IKCPSEG *seg) {
// flush acknowledges
	int count = kcp->ackcount;
	int size = 0;

	/// 将所有ack都发送出去
	for (i = 0; i < count; i++) {
		size = (int)(ptr - buffer);
		/// 只有凑满一个mtu，才发送
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		/// sn:message分片segment的序号
		/// ts:message发送时刻的时间戳
		ikcp_ack_get(kcp, i, &seg->sn, &seg->ts);
		ptr = ikcp_encode_seg(ptr, seg);
	}

	kcp->ackcount = 0;
}

/// 设置探测窗口信息
void ikcp_set_probe_remote_wnd_info(ikcpcb *kcp) {
	/// 远端接收窗口大小为0的时候
	if (kcp->rmt_wnd == 0) {
		/// 探查窗口需要等待的时间为0
		if (kcp->probe_wait == 0) {
			/// 设置探查窗口需要等待的时间
			kcp->probe_wait = IKCP_PROBE_INIT;
			/// 设置下次探查窗口的时间戳=当前时间+探查窗口等待时间间隔
			kcp->ts_probe = kcp->current + kcp->probe_wait;
		}	
		else {
			/// 当前时间 > 下一次探查窗口的时间
			if (_itimediff(kcp->current, kcp->ts_probe) >= 0) {
				if (kcp->probe_wait < IKCP_PROBE_INIT) 
					kcp->probe_wait = IKCP_PROBE_INIT;
				/// 等待时间变为之前的1.5倍
				kcp->probe_wait += kcp->probe_wait / 2;
				if (kcp->probe_wait > IKCP_PROBE_LIMIT)
					/// 若超过上限值，设置为上限值
					kcp->probe_wait = IKCP_PROBE_LIMIT;
				/// 计算下次探查窗口的时间戳
				kcp->ts_probe = kcp->current + kcp->probe_wait;
				/// 设置探查变量。IKCP_ASK_TELL表示告知远端窗口大小。
				/// IKCP_ASK_SEND表示请求远端告知窗口大小
				kcp->probe |= IKCP_ASK_SEND;
			}
		}
	}	else {
		kcp->ts_probe = 0;
		kcp->probe_wait = 0;
	}
}

void ikcp_send_probe_remote_wnd_info(ikcpcb *kcp, , IKCPSEG *seg, char *ptr) {
	char *buffer = kcp->buffer;

	if (kcp->probe & IKCP_ASK_SEND) {
		seg->cmd = IKCP_CMD_WASK;
		int size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, seg);
	}
}

void ikcp_tell_remote_wnd_info(ikcpcb *kcp, , IKCPSEG *seg, char *ptr) {
	char *buffer = kcp->buffer;

	if (kcp->probe & IKCP_ASK_TELL) {
		seg->cmd = IKCP_CMD_WINS;
		int size = (int)(ptr - buffer);
		if (size + (int)IKCP_OVERHEAD > (int)kcp->mtu) {
			ikcp_output(kcp, buffer, size);
			ptr = buffer;
		}
		ptr = ikcp_encode_seg(ptr, seg);
	}
}

void ikcp_wrap_send_push_msg(ikcpcb *kcp, IKCPSEG *newseg) {
		newseg->conv = kcp->conv;
		/// IKCP_CMD_PUSH: 数据分片
		/// IKCP_CMD_ACK: ack分片
		/// IKCP_CMD_WASK： 请求告知窗口大小
		/// IKCP_CMD_WINS: 告知窗口大小
		newseg->cmd = IKCP_CMD_PUSH;
		/// 接收窗口的大小 - 接收队列的大小
		/// 告知远程本地剩余的窗口数量
		newseg->wnd = seg.wnd;
		/// 当前的时间戳
		newseg->ts = current;
		/// 下一个待发报的序号
		newseg->sn = kcp->snd_nxt++;
		/// 待确认的消息序号，即下一个待接收的包，rcv_nxt前面的包已经接收成功
		newseg->una = kcp->rcv_nxt;
		/// 下次超时重传的时间戳
		newseg->resendts = kcp->current;
		/// 由ack接收延迟计算出来的重传超时时间
		newseg->rto = kcp->rx_rto;
		/// 收到ack时计算的该分片被跳过的累计次数
		newseg->fastack = 0;
		/// 发送分片的次数，每发送一次加一
		newseg->xmit = 0;
}

void ikcp_check_to_send_push_msg(ikcpcb *kcp, IUINT32 cwnd) {
	// move data from snd_queue to snd_buf
	/// 表明拥塞窗口还有空间，可以继续发送
	while (_itimediff(kcp->snd_nxt, kcp->snd_una + cwnd) < 0) {
		IKCPSEG *newseg;
		/// 发送队列为空，不需要发送
		if (iqueue_is_empty(&kcp->snd_queue)) break;

		/// snd_queue: 发送消息的队列
		/// 从头部获取待发送的包
		newseg = iqueue_entry(kcp->snd_queue.next, IKCPSEG, node);

		/// 将待发送的包，从待发送队列中删除
		iqueue_del(&newseg->node);
		/// snd_buf：发送缓冲队列
		/// 将待发送的包，放进发送缓冲队列中
		iqueue_add_tail(&newseg->node, &kcp->snd_buf);
		kcp->nsnd_que--;
		kcp->nsnd_buf++;

		ikcp_wrap_send_push_msg(kcp, newseg);
	}
}

IUINT32 ikcp_calculate_window_size(ikcpcb *kcp) {
	/// cwnd = 发送窗口大小 和 远端接收窗口大小的最小值
	IUINT32 cwnd = _imin_(kcp->snd_wnd, kcp->rmt_wnd);
	/// == 0， 表示不取消拥塞控制
	if (kcp->nocwnd == 0) 
		/// 拥塞窗口 = 当前拥塞窗口和cwnd的最小值（也就是取当前拥塞窗口、发送窗口、接收窗口的最小值）
		cwnd = _imin_(kcp->cwnd, cwnd);

	return cwnd;
}

//---------------------------------------------------------------------
// ikcp_flush
//---------------------------------------------------------------------
void ikcp_flush(ikcpcb *kcp)
{
	IUINT32 current = kcp->current;
	char *buffer = kcp->buffer;
	char *ptr = buffer;
	int count, size, i;
	IUINT32 resent, cwnd;
	IUINT32 rtomin;
	struct IQUEUEHEAD *p;
	int change = 0;
	int lost = 0;
	IKCPSEG seg;

	// 'ikcp_update' haven't been called. 
	if (kcp->updated == 0) return;

	seg.conv = kcp->conv;
	seg.cmd = IKCP_CMD_ACK;
	seg.frg = 0;
	/// 接收窗口的大小 - 接收队列的大小
	seg.wnd = ikcp_wnd_unused(kcp);
	seg.una = kcp->rcv_nxt;
	seg.len = 0;
	seg.sn = 0;
	seg.ts = 0;
	
	/// 发送ack队列中的所有ack
	ikcp_send_ack(kcp, &seg);

	// probe window size (if remote window size equals zero)
	/// 设置探测窗口信息
	ikcp_set_probe_remote_wnd_info(kcp);

	// flush window probing commands
	/// 请求远端告知窗口大小
	ikcp_send_probe_remote_wnd_info(kcp, &seg, ptr);

	// flush window probing commands
	/// 告知远端窗口大小
	ikcp_tell_remote_wnd_info(kcp, &seg, ptr);
	kcp->probe = 0;

	// calculate window size
	cwnd = ikcp_calculate_window_size(kcp);

	ikcp_check_to_send_push_msg(kcp, cwnd);

	// calculate resent
	resent = (kcp->fastresend > 0)? (IUINT32)kcp->fastresend : 0xffffffff;
	/// 最小值是原值的1/8
	rtomin = (kcp->nodelay == 0)? (kcp->rx_rto >> 3) : 0;

	// flush data segments
	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		IKCPSEG *segment = iqueue_entry(p, IKCPSEG, node);
		int needsend = 0;
		if (segment->xmit == 0) {
			needsend = 1;
			/// 发送分片的次数
			segment->xmit++;
			/// 该分片超时重传的时间戳
			segment->rto = kcp->rx_rto;
			/// 下次超时重传的时间戳
			segment->resendts = current + segment->rto + rtomin;
		}
		/// 当前时间 > 下次重传时间。
		/// 说明在等待了resendts时间后，还没有收到ACK消息，表明该包丢了，需要重传
		else if (_itimediff(current, segment->resendts) >= 0) {
			needsend = 1;
			segment->xmit++;
			kcp->xmit++;
			/// 0表示不启动快速重传模式
			if (kcp->nodelay == 0) {
				/// 不启动快速重传模式，每次重传之后rto的时间就是之前的2倍
				segment->rto += kcp->rx_rto;
			}	else {
				/// 启用重传模式之后，rto变成原来的1.5倍
				segment->rto += kcp->rx_rto / 2;
			}
			segment->resendts = current + segment->rto;
			/// 丢包了
			lost = 1;
		}
		/// fastack: 表示收到ack计算的该分片被跳过的累积次数，
		///          需要快速重传了
		else if (segment->fastack >= resent) {
			needsend = 1;
			segment->xmit++;
			segment->fastack = 0;
			/// 快速重传时，rto没有变化
			segment->resendts = current + segment->rto;
			change++;
		}

		if (needsend) {
			int size, need;
			/// 设置时间戳
			segment->ts = current;
			/// 剩余接收窗口大小。即接收窗口大小-接收队列大小
			segment->wnd = seg.wnd;
			/// 待接收消息序号
			segment->una = kcp->rcv_nxt;

			size = (int)(ptr - buffer);
			/// segment报文默认大小 + segment的长度
			need = IKCP_OVERHEAD + segment->len;

			/// 禁止数据合包
			if (size + need > (int)kcp->mtu) {
				/// 发送数据包
				ikcp_output(kcp, buffer, size);
				ptr = buffer;
			}

			ptr = ikcp_encode_seg(ptr, segment);

			if (segment->len > 0) {
				memcpy(ptr, segment->data, segment->len);
				ptr += segment->len;
			}

			if (segment->xmit >= kcp->dead_link) {
				kcp->state = -1;
			}
		}
	}

	// flash remain segments
	size = (int)(ptr - buffer);
	if (size > 0) {
		ikcp_output(kcp, buffer, size);
	}

	// update ssthresh
	if (change) {
		/// 下一个要分配的包 - 第一个未确认的包
		IUINT32 inflight = kcp->snd_nxt - kcp->snd_una;
		/// change=1说明发生过快速重传。当发生快速重传的时候，会将慢启动阀值
		/// 调整为当前发送窗口的一半
		kcp->ssthresh = inflight / 2;
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		/// 并把拥塞窗口大小 = 拥塞窗口阀值 + 触发快速重传的ack大小
		kcp->cwnd = kcp->ssthresh + resent;
		kcp->incr = kcp->cwnd * kcp->mss;
	}

	if (lost) {
		/// 丢包了。窗口的大小需要减半
		kcp->ssthresh = cwnd / 2;
		if (kcp->ssthresh < IKCP_THRESH_MIN)
			kcp->ssthresh = IKCP_THRESH_MIN;
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}

	if (kcp->cwnd < 1) {
		kcp->cwnd = 1;
		kcp->incr = kcp->mss;
	}
}


//---------------------------------------------------------------------
// update state (call it repeatedly, every 10ms-100ms), or you can ask 
// ikcp_check when to call it again (without ikcp_input/_send calling).
// 'current' - current timestamp in millisec. 
//---------------------------------------------------------------------
void ikcp_update(ikcpcb *kcp, IUINT32 current)
{
	IINT32 slap;

	kcp->current = current;

	if (kcp->updated == 0) {
		kcp->updated = 1;
		kcp->ts_flush = kcp->current;
	}

	slap = _itimediff(kcp->current, kcp->ts_flush);

	if (slap >= 10000 || slap < -10000) {
		kcp->ts_flush = kcp->current;
		slap = 0;
	}

	if (slap >= 0) {
		kcp->ts_flush += kcp->interval;
		if (_itimediff(kcp->current, kcp->ts_flush) >= 0) {
			kcp->ts_flush = kcp->current + kcp->interval;
		}
		ikcp_flush(kcp);
	}
}


//---------------------------------------------------------------------
// Determine when should you invoke ikcp_update:
// returns when you should invoke ikcp_update in millisec, if there 
// is no ikcp_input/_send calling. you can call ikcp_update in that
// time, instead of call update repeatly.
// Important to reduce unnacessary ikcp_update invoking. use it to 
// schedule ikcp_update (eg. implementing an epoll-like mechanism, 
// or optimize ikcp_update when handling massive kcp connections)
//---------------------------------------------------------------------
IUINT32 ikcp_check(const ikcpcb *kcp, IUINT32 current)
{
	IUINT32 ts_flush = kcp->ts_flush;
	IINT32 tm_flush = 0x7fffffff;
	IINT32 tm_packet = 0x7fffffff;
	IUINT32 minimal = 0;
	struct IQUEUEHEAD *p;

	if (kcp->updated == 0) {
		return current;
	}

	if (_itimediff(current, ts_flush) >= 10000 ||
		_itimediff(current, ts_flush) < -10000) {
		ts_flush = current;
	}

	if (_itimediff(current, ts_flush) >= 0) {
		return current;
	}

	tm_flush = _itimediff(ts_flush, current);

	for (p = kcp->snd_buf.next; p != &kcp->snd_buf; p = p->next) {
		const IKCPSEG *seg = iqueue_entry(p, const IKCPSEG, node);
		IINT32 diff = _itimediff(seg->resendts, current);
		if (diff <= 0) {
			return current;
		}
		if (diff < tm_packet) tm_packet = diff;
	}

	minimal = (IUINT32)(tm_packet < tm_flush ? tm_packet : tm_flush);
	if (minimal >= kcp->interval) minimal = kcp->interval;

	return current + minimal;
}



int ikcp_setmtu(ikcpcb *kcp, int mtu)
{
	char *buffer;
	if (mtu < 50 || mtu < (int)IKCP_OVERHEAD) 
		return -1;
	buffer = (char*)ikcp_malloc((mtu + IKCP_OVERHEAD) * 3);
	if (buffer == NULL) 
		return -2;
	kcp->mtu = mtu;
	kcp->mss = kcp->mtu - IKCP_OVERHEAD;
	ikcp_free(kcp->buffer);
	kcp->buffer = buffer;
	return 0;
}

int ikcp_interval(ikcpcb *kcp, int interval)
{
	if (interval > 5000) interval = 5000;
	else if (interval < 10) interval = 10;
	kcp->interval = interval;
	return 0;
}

/// nodelay:  0 不启用，  1 启用快速重传模式
/// interval: 内部flush刷新时间
/// resend:   0(默认)表示关闭。可以设置值，若设置为2（则2次ACK跨越将会直接重传)
/// nc:       是否关闭拥塞控制， 0(默认)代表不关闭，1代表关闭
int ikcp_nodelay(ikcpcb *kcp, int nodelay, int interval, int resend, int nc)
{
	/// 大于0表示启用快速重传模式
	if (nodelay >= 0) {
		kcp->nodelay = nodelay;
		if (nodelay) {
			kcp->rx_minrto = IKCP_RTO_NDL;	
		}	
		else {
			kcp->rx_minrto = IKCP_RTO_MIN;
		}
	}
	if (interval >= 0) {
		if (interval > 5000) interval = 5000;
		else if (interval < 10) interval = 10;
		/// 内部flush刷新时间
		kcp->interval = interval;
	}

	/// fast resend的阀值
	/// ACK被跳过resend次数后直接重传该包，而不等待超时
	if (resend >= 0) {
		kcp->fastresend = resend;
	}
	/// 大于0，表示关闭
	if (nc >= 0) {
		kcp->nocwnd = nc;
	}
	return 0;
}


int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd)
{
	if (kcp) {
		if (sndwnd > 0) {
			kcp->snd_wnd = sndwnd;
		}
		if (rcvwnd > 0) {   // must >= max fragment size
			kcp->rcv_wnd = _imax_(rcvwnd, IKCP_WND_RCV);
		}
	}
	return 0;
}

int ikcp_waitsnd(const ikcpcb *kcp)
{
	return kcp->nsnd_buf + kcp->nsnd_que;
}


// read conv
IUINT32 ikcp_getconv(const void *ptr)
{
	IUINT32 conv;
	ikcp_decode32u((const char*)ptr, &conv);
	return conv;
}


