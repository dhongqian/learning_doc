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
#ifndef __IKCP_H__
#define __IKCP_H__

#include <stddef.h>
#include <stdlib.h>
#include <assert.h>


//=====================================================================
// 32BIT INTEGER DEFINITION 
//=====================================================================
#ifndef __INTEGER_32_BITS__
#define __INTEGER_32_BITS__
#if defined(_WIN64) || defined(WIN64) || defined(__amd64__) || \
	defined(__x86_64) || defined(__x86_64__) || defined(_M_IA64) || \
	defined(_M_AMD64)
	typedef unsigned int ISTDUINT32;
	typedef int ISTDINT32;
#elif defined(_WIN32) || defined(WIN32) || defined(__i386__) || \
	defined(__i386) || defined(_M_X86)
	typedef unsigned long ISTDUINT32;
	typedef long ISTDINT32;
#elif defined(__MACOS__)
	typedef UInt32 ISTDUINT32;
	typedef SInt32 ISTDINT32;
#elif defined(__APPLE__) && defined(__MACH__)
	#include <sys/types.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif defined(__BEOS__)
	#include <sys/inttypes.h>
	typedef u_int32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#elif (defined(_MSC_VER) || defined(__BORLANDC__)) && (!defined(__MSDOS__))
	typedef unsigned __int32 ISTDUINT32;
	typedef __int32 ISTDINT32;
#elif defined(__GNUC__)
	#include <stdint.h>
	typedef uint32_t ISTDUINT32;
	typedef int32_t ISTDINT32;
#else 
	typedef unsigned long ISTDUINT32; 
	typedef long ISTDINT32;
#endif
#endif


//=====================================================================
// Integer Definition
//=====================================================================
#ifndef __IINT8_DEFINED
#define __IINT8_DEFINED
typedef char IINT8;
#endif

#ifndef __IUINT8_DEFINED
#define __IUINT8_DEFINED
typedef unsigned char IUINT8;
#endif

#ifndef __IUINT16_DEFINED
#define __IUINT16_DEFINED
typedef unsigned short IUINT16;
#endif

#ifndef __IINT16_DEFINED
#define __IINT16_DEFINED
typedef short IINT16;
#endif

#ifndef __IINT32_DEFINED
#define __IINT32_DEFINED
typedef ISTDINT32 IINT32;
#endif

#ifndef __IUINT32_DEFINED
#define __IUINT32_DEFINED
typedef ISTDUINT32 IUINT32;
#endif

#ifndef __IINT64_DEFINED
#define __IINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef __int64 IINT64;
#else
typedef long long IINT64;
#endif
#endif

#ifndef __IUINT64_DEFINED
#define __IUINT64_DEFINED
#if defined(_MSC_VER) || defined(__BORLANDC__)
typedef unsigned __int64 IUINT64;
#else
typedef unsigned long long IUINT64;
#endif
#endif

#ifndef INLINE
#if defined(__GNUC__)

#if (__GNUC__ > 3) || ((__GNUC__ == 3) && (__GNUC_MINOR__ >= 1))
#define INLINE         __inline__ __attribute__((always_inline))
#else
#define INLINE         __inline__
#endif

#elif (defined(_MSC_VER) || defined(__BORLANDC__) || defined(__WATCOMC__))
#define INLINE __inline
#else
#define INLINE 
#endif
#endif

#if (!defined(__cplusplus)) && (!defined(inline))
#define inline INLINE
#endif


//=====================================================================
// QUEUE DEFINITION                                                  
//=====================================================================
#ifndef __IQUEUE_DEF__
#define __IQUEUE_DEF__

struct IQUEUEHEAD {
	struct IQUEUEHEAD *next, *prev;
};

typedef struct IQUEUEHEAD iqueue_head;


//---------------------------------------------------------------------
// queue init                                                         
//---------------------------------------------------------------------
#define IQUEUE_HEAD_INIT(name) { &(name), &(name) }
#define IQUEUE_HEAD(name) \
	struct IQUEUEHEAD name = IQUEUE_HEAD_INIT(name)

#define IQUEUE_INIT(ptr) ( \
	(ptr)->next = (ptr), (ptr)->prev = (ptr))

#define IOFFSETOF(TYPE, MEMBER) ((size_t) &((TYPE *)0)->MEMBER)

#define ICONTAINEROF(ptr, type, member) ( \
		(type*)( ((char*)((type*)ptr)) - IOFFSETOF(type, member)) )

#define IQUEUE_ENTRY(ptr, type, member) ICONTAINEROF(ptr, type, member)


//---------------------------------------------------------------------
// queue operation                     
//---------------------------------------------------------------------
#define IQUEUE_ADD(node, head) ( \
	(node)->prev = (head), (node)->next = (head)->next, \
	(head)->next->prev = (node), (head)->next = (node))

#define IQUEUE_ADD_TAIL(node, head) ( \
	(node)->prev = (head)->prev, (node)->next = (head), \
	(head)->prev->next = (node), (head)->prev = (node))

#define IQUEUE_DEL_BETWEEN(p, n) ((n)->prev = (p), (p)->next = (n))

#define IQUEUE_DEL(entry) (\
	(entry)->next->prev = (entry)->prev, \
	(entry)->prev->next = (entry)->next, \
	(entry)->next = 0, (entry)->prev = 0)

#define IQUEUE_DEL_INIT(entry) do { \
	IQUEUE_DEL(entry); IQUEUE_INIT(entry); } while (0)

#define IQUEUE_IS_EMPTY(entry) ((entry) == (entry)->next)

#define iqueue_init		IQUEUE_INIT
#define iqueue_entry	IQUEUE_ENTRY
#define iqueue_add		IQUEUE_ADD
#define iqueue_add_tail	IQUEUE_ADD_TAIL
#define iqueue_del		IQUEUE_DEL
#define iqueue_del_init	IQUEUE_DEL_INIT
#define iqueue_is_empty IQUEUE_IS_EMPTY

#define IQUEUE_FOREACH(iterator, head, TYPE, MEMBER) \
	for ((iterator) = iqueue_entry((head)->next, TYPE, MEMBER); \
		&((iterator)->MEMBER) != (head); \
		(iterator) = iqueue_entry((iterator)->MEMBER.next, TYPE, MEMBER))

#define iqueue_foreach(iterator, head, TYPE, MEMBER) \
	IQUEUE_FOREACH(iterator, head, TYPE, MEMBER)

#define iqueue_foreach_entry(pos, head) \
	for( (pos) = (head)->next; (pos) != (head) ; (pos) = (pos)->next )
	
/// 把list踢出去了，
/// first和last放在了head和at中间
#define __iqueue_splice(list, head) do {	\
		iqueue_head *first = (list)->next, *last = (list)->prev; \
		iqueue_head *at = (head)->next; \
		(first)->prev = (head), (head)->next = (first);		\
		(last)->next = (at), (at)->prev = (last); }	while (0)

#define iqueue_splice(list, head) do { \
	if (!iqueue_is_empty(list)) __iqueue_splice(list, head); } while (0)

#define iqueue_splice_init(list, head) do {	\
	iqueue_splice(list, head);	iqueue_init(list); } while (0)


#ifdef _MSC_VER
#pragma warning(disable:4311)
#pragma warning(disable:4312)
#pragma warning(disable:4996)
#endif 

#endif


//---------------------------------------------------------------------
// WORD ORDER
//---------------------------------------------------------------------
#ifndef IWORDS_BIG_ENDIAN
    #ifdef _BIG_ENDIAN_
        #if _BIG_ENDIAN_
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #if defined(__hppa__) || \
            defined(__m68k__) || defined(mc68000) || defined(_M_M68K) || \
            (defined(__MIPS__) && defined(__MIPSEB__)) || \
            defined(__ppc__) || defined(__POWERPC__) || defined(_M_PPC) || \
            defined(__sparc__) || defined(__powerpc__) || \
            defined(__mc68000__) || defined(__s390x__) || defined(__s390__)
            #define IWORDS_BIG_ENDIAN 1
        #endif
    #endif
    #ifndef IWORDS_BIG_ENDIAN
        #define IWORDS_BIG_ENDIAN  0
    #endif
#endif



//=====================================================================
// SEGMENT
//=====================================================================
struct IKCPSEG
{
	struct IQUEUEHEAD node;
	IUINT32 conv;
	/// IKCP_CMD_PUSH: 数据分片
	/// IKCP_CMD_ACK: ack分片
	/// IKCP_CMD_WASK： 请求告知窗口大小
	/// IKCP_CMD_WINS: 告知窗口大小
	IUINT32 cmd;
	/// (kcp->stream == 0)? (count - i - 1) : 0
	/// 流模式：值为0，表明没有分片的概念
	/// 非流模式：比如分为5个片，当前是第3个分片（从0开始算）
	/// frg值是：5-3-1=1
	/// 第0个分片的frg值是:5-0-1=4
	/// 第4个分片的frg值是:5-4-1=0
	/// 所以当接收分片的时候，当frg为0，说明分片都接收完成
	IUINT32 frg;
	/// 接收窗口的大小 - 接收队列的大小
	/// 告知远程本地剩余的窗口数量
	IUINT32 wnd;
	/// timestamp
	/// 当前的时间戳
	IUINT32 ts;
	/// serial number
	/// 下一个待发包的序号
	IUINT32 sn;
	/// 已发送但未确认的序列号
	/// 待确认的消息序号，即下一个待接收的包，rcv_nxt前面的包已经接收成功
	IUINT32 una;
	/// 记录当前发送的data的大小
	IUINT32 len;
	/// 下次超时重传的时间戳
	IUINT32 resendts;
	/// Retransmission Timeout 超时重传时间,应略大于rtt
	/// rtt: Round-Trip Time 从发送端发送数据开始，
	///      到发送端收到来自接收端的确认（接收端收到数据后便立即发送确认），
	///      总共经历的时延
	/// rto: 重传超时时间, 发送数据包在一定的时间内没有收到相应的ACK，
	///      等待一定的时间，超时之后就认为这个数据包丢失，就会重新发送。
	///      这个等待时间被称为RTO
	///      KCP的RTO可以以1.5倍的速度增长,TCP是2倍
	/// 由ack接收延迟计算出来的重传超时时间
	IUINT32 rto;
	/// 快速重传机制—无延迟ACK回复模式
	/// 假如开启KCP的快速重传机制，并且设置了当重复的ACK个数大于resend时候，
	/// 直接进行重传。 当发送端发送了1,2,3,4,5五个包，然后收到远端的ACK：
	/// 1,3,4,5。当收到ACK3时，KCP知道2被跳过1次，当收到ACK4的时候，
	/// KCP知道2被跳过2次，当次数大于等于设置的resend的值的时候，不用等到
	/// 超时，可直接重传2号包。这就是KCP的快速重传机制
	/// 收到ack时计算的该分片被跳过的累计次数
	IUINT32 fastack;
	/// 发送分片的次数，每发送一次，加一
	IUINT32 xmit;
	char data[1];
};


//---------------------------------------------------------------------
// IKCPCB
//---------------------------------------------------------------------
struct IKCPCB
{
	IUINT32 conv, mtu, mss, state;
	/// snd_una：第一个未确认的包
	/// snd_nxt：下一个待分配的包的序号
	/// rcv_nxt: 下一个要接收的包，即rcv_nxt前面的包，已接收成功
	IUINT32 snd_una, snd_nxt, rcv_nxt;
	IUINT32 ts_recent, ts_lastack, ssthresh;
	IINT32 rx_rttval, rx_srtt, rx_rto, rx_minrto;
	/// snd_wnd: 发送窗口大小
	/// rmt_wnd: 远端接收窗口大小
	/// rcv_wnd: 接收窗口大小
	/// cwnd: 当前的拥塞窗口
	IUINT32 snd_wnd, rcv_wnd, rmt_wnd, cwnd, probe;
	/// current: update的时候，被设置
	IUINT32 current, interval, ts_flush, xmit;
	/// nsnd_buf: 发送缓冲队列中包的数量
	IUINT32 nrcv_buf, nsnd_buf;
	/// nsnd_que: 待发送队列的IKCPSEG数量
	IUINT32 nrcv_que, nsnd_que;
	IUINT32 nodelay, updated;
	IUINT32 ts_probe, probe_wait;
	IUINT32 dead_link, incr;
	/// 待发送队列，成员是IKCPSEG 
	struct IQUEUEHEAD snd_queue;
	struct IQUEUEHEAD rcv_queue;
	/// 发送缓冲队列
	struct IQUEUEHEAD snd_buf;
	struct IQUEUEHEAD rcv_buf;
	/// 发送确认报文的缓冲列表
	IUINT32 *acklist;
	IUINT32 ackcount;
	IUINT32 ackblock;
	void *user;
	/// 缓冲区
	char *buffer;
	/// 假设fastresend=2
	/// 当发送端发送了1,2,3,4,5五个包，然后收到远端的ACK：
	/// 1,3,4,5。当收到ACK3时，KCP知道2被跳过1次，当收到ACK4的时候，
	/// KCP知道2被跳过2次，大于等于fastresend(=2)，不用等到
	/// 超时，可直接重传2号包。这就是KCP的快速重传机制
	/// 收到ack时计算的该分片被跳过的累计次数
	int fastresend;
	/// nocwnd: 是否取消拥塞控制， 0 不取消， 1 取消
	int nocwnd, stream;
	int logmask;
	int (*output)(const char *buf, int len, struct IKCPCB *kcp, void *user);
	void (*writelog)(const char *log, struct IKCPCB *kcp, void *user);
};


typedef struct IKCPCB ikcpcb;

#define IKCP_LOG_OUTPUT			1
#define IKCP_LOG_INPUT			2
#define IKCP_LOG_SEND			4
#define IKCP_LOG_RECV			8
#define IKCP_LOG_IN_DATA		16
#define IKCP_LOG_IN_ACK			32
#define IKCP_LOG_IN_PROBE		64
#define IKCP_LOG_IN_WINS		128
#define IKCP_LOG_OUT_DATA		256
#define IKCP_LOG_OUT_ACK		512
#define IKCP_LOG_OUT_PROBE		1024
#define IKCP_LOG_OUT_WINS		2048

#ifdef __cplusplus
extern "C" {
#endif

//---------------------------------------------------------------------
// interface
//---------------------------------------------------------------------

// create a new kcp control object, 'conv' must equal in two endpoint
// from the same connection. 'user' will be passed to the output callback
// output callback can be setup like this: 'kcp->output = my_udp_output'
ikcpcb* ikcp_create(IUINT32 conv, void *user);

// release kcp control object
void ikcp_release(ikcpcb *kcp);

// set output callback, which will be invoked by kcp
void ikcp_setoutput(ikcpcb *kcp, int (*output)(const char *buf, int len, 
	ikcpcb *kcp, void *user));

// user/upper level recv: returns size, returns below zero for EAGAIN
int ikcp_recv(ikcpcb *kcp, char *buffer, int len);

// user/upper level send, returns below zero for error
int ikcp_send(ikcpcb *kcp, const char *buffer, int len);

// update state (call it repeatedly, every 10ms-100ms), or you can ask 
// ikcp_check when to call it again (without ikcp_input/_send calling).
// 'current' - current timestamp in millisec. 
void ikcp_update(ikcpcb *kcp, IUINT32 current);

// Determine when should you invoke ikcp_update:
// returns when you should invoke ikcp_update in millisec, if there 
// is no ikcp_input/_send calling. you can call ikcp_update in that
// time, instead of call update repeatly.
// Important to reduce unnacessary ikcp_update invoking. use it to 
// schedule ikcp_update (eg. implementing an epoll-like mechanism, 
// or optimize ikcp_update when handling massive kcp connections)
IUINT32 ikcp_check(const ikcpcb *kcp, IUINT32 current);

// when you received a low level packet (eg. UDP packet), call it
int ikcp_input(ikcpcb *kcp, const char *data, long size);

// flush pending data
void ikcp_flush(ikcpcb *kcp);

// check the size of next message in the recv queue
int ikcp_peeksize(const ikcpcb *kcp);

// change MTU size, default is 1400
int ikcp_setmtu(ikcpcb *kcp, int mtu);

// set maximum window size: sndwnd=32, rcvwnd=32 by default
int ikcp_wndsize(ikcpcb *kcp, int sndwnd, int rcvwnd);

// get how many packet is waiting to be sent
int ikcp_waitsnd(const ikcpcb *kcp);

// fastest: ikcp_nodelay(kcp, 1, 20, 2, 1)
// nodelay: 0:disable(default), 1:enable
// interval: internal update timer interval in millisec, default is 100ms 
// resend: 0:disable fast resend(default), 1:enable fast resend
// nc: 0:normal congestion control(default), 1:disable congestion control
int ikcp_nodelay(ikcpcb *kcp, int nodelay, int interval, int resend, int nc);


void ikcp_log(ikcpcb *kcp, int mask, const char *fmt, ...);

// setup allocator
void ikcp_allocator(void* (*new_malloc)(size_t), void (*new_free)(void*));

// read conv
IUINT32 ikcp_getconv(const void *ptr);

/// refact by dhongqian
void ikcp_send_ack(ikcpcb *kcp, IKCPSEG *seg);
void ikcp_set_probe_remote_wnd_info(ikcpcb *kcp);
void ikcp_send_probe_remote_wnd_info(ikcpcb *kcp, , IKCPSEG *seg, char *ptr);
void ikcp_tell_remote_wnd_info(ikcpcb *kcp, , IKCPSEG *seg, char *ptr);
void ikcp_wrap_send_push_msg(ikcpcb *kcp, IKCPSEG *newseg);
void ikcp_check_to_send_push_msg(ikcpcb *kcp, IUINT32 cwnd);
IUINT32 ikcp_calculate_window_size(ikcpcb *kcp);


#ifdef __cplusplus
}
#endif

#endif


