/* 
  A version of malloc/free/realloc written by Doug Lea and released to the 
  public domain. 

  VERSION 2.5.1

  working version; unreleased.

* History:
    Based loosely on libg++-1.2X malloc. (It retains some of the overall 
       structure of old version,  but most details differ.)

    Trial version Fri Aug 28 13:14:29 1992  Doug Lea  (dl at g.oswego.edu)

    V2.5 Sat Aug  7 07:41:59 1993  Doug Lea  (dl at g.oswego.edu)
      * removed potential for odd address access in prev_chunk
      * removed dependency on getpagesize.h
      * misc cosmetics and a bit more internal documentation
      * anticosmetics: mangled names in macros to evade debugger strangeness
      * tested on sparc, hp-700, dec-mips, rs6000 
          with gcc & native cc (hp, dec only) allowing
          Detlefs & Zorn comparison study (to appear, SIGPLAN Notices.)

    V2.5.1 Sat Aug 14 15:40:43 1993  Doug Lea  (dl at g)
      * faster bin computation & slightly different binning
      * merged all consolidations to one part of malloc proper
         (eliminating old malloc_find_space & malloc_clean_bin)
      * Scan 2 returned_list chunks (not just 1)

     Sat Apr  2 06:51:25 1994  Doug Lea  (dl at g)
      * Propagate failure in realloc if malloc returns 0
      * Add stuff to allow compilation on non-ANSI compilers
     
* Overview

  This malloc, like any other, is a compromised design. 

  Chunks of memory are maintained using a `boundary tag' method as
  described in e.g., Knuth or Standish.  The size of the chunk is
  stored both in the front of the chunk and at the end.  This makes
  consolidating fragmented chunks into bigger chunks very fast.  The
  size field also hold a bit representing whether a chunk is free or
  in use.

  Malloced chunks have space overhead of 8 bytes: The preceding and
  trailing size fields.  When a chunk is freed, 8 additional bytes are
  needed for free list pointers. Thus, the minimum allocatable size is
  16 bytes.  8 byte alignment is currently hardwired into the design.
  This seems to suffice for all current machines and C compilers.
  Calling memalign will return a chunk that is both 8-byte aligned
  and meets the requested (power of two) alignment.

  It is assumed that 32 bits suffice to represent chunk sizes.  The
  maximum size chunk is 2^31 - 8 bytes.  malloc(0) returns a pointer
  to something of the minimum allocatable size.  Requests for negative
  sizes (when size_t is signed) or with the highest bit set (when
  unsigned) will also return a minimum-sized chunk.

  Available chunks are kept in doubly linked lists. The lists are
  maintained in an array of bins using approximately proportionally
  spaced bins.  There are a lot of bins (128). This may look
  excessive, but works very well in practice.  The use of very fine
  bin sizes closely approximates the use of one bin per actually used
  size, without necessitating the overhead of locating such bins. It
  is especially desirable in common applications where large numbers
  of identically-sized blocks are malloced/freed in some dynamic
  manner, and then later are all freed.  The finer bin sizes make
  finding blocks fast, with little wasted overallocation. The
  consolidation methods ensure that once the collection of blocks is
  no longer useful, fragments are gathered into bigger chunks awaiting
  new roles.

  The bins av[i] serve as heads of the lists. Bins contain a dummy
  header for the chunk lists. Each bin has two lists. The `dirty' list
  holds chunks that have been returned (freed) and not yet either
  re-malloc'ed or consolidated. (A third free-standing list contains
  returned chunks that have not yet been processed at all.) The `clean'
  list holds split-off fragments and consolidated space. All
  procedures maintain the invariant that no clean chunk physically
  borders another clean chunk. Thus, clean chunks never need to be
  scanned during consolidation.

* Algorithms

  Malloc:

    This is a very heavily disguised first-fit algorithm.
    Most of the heuristics are designed to maximize the likelihood
    that a usable chunk will most often be found very quickly,
    while still minimizing fragmentation and overhead.

    The allocation strategy has several phases:

      0. Convert the request size into a usable form. This currently
         means to add 8 bytes overhead plus possibly more to obtain
         8-byte alignment. Call this size `nb'.

      1. Check if either of the last 2 returned (free()'d) or
         preallocated chunk are of the exact size nb. If so, use one.
         `Exact' means no more than MINSIZE (currently 16) bytes
         larger than nb. This cannot be reduced, since a chunk with
         size < MINSIZE cannot be created to hold the remainder.

         This check need not fire very often to be effective.  It
         reduces overhead for sequences of requests for the same
         preallocated size to a dead minimum.

      2. Look for a chunk in the dirty bin associated with nb.
         `Dirty' chunks are those that have never been consolidated.
         Besides the fact that they, but not clean chunks require
         scanning for consolidation, these chunks are of sizes likely
         to be useful because they have been previously requested and
         then freed by the user program.

         Dirty chunks of bad sizes (even if too big) are never used
         without consolidation. Among other things, this maintains the
         invariant that split chunks (see below) are ALWAYS clean.

      3. Scan clean chunks in the bin, choosing any of size >= nb.
         Split later if necessary (step 9) below.  (Unlike in step 2,
         it is good to split here, because it creates a chunk of a
         known-to-be-useful size out of a fragment that happened to be
         close in size.)

      4. Pull other requests off the returned chunk list, using one if
         it is of exact size, else distributing into the appropriate
         bins.

      5. Try to use the last chunk remaindered during a previous
         malloc. (The ptr to this chunk is kept in var last_remainder,
         to make it easy to find and to avoid useless re-binning
         during repeated splits.  The code surrounding it is fairly
         delicate. This chunk must be pulled out and placed in a bin
         prior to any consolidation, to avoid having other pointers
         point into the middle of it, or try to unlink it.) If
         it is usable, proceed to split.

      6. Scan through the clean lists of all larger bins, selecting
         any chunk at all. (It will surely be big enough since it is
         in a bigger bin.)  The scan goes upward from small bins to
         large.  It would be faster downward, but could lead to excess
         fragmentation. If successful, proceed to split.

      7. Consolidate chunks in other dirty bins until a large enough
         chunk is created. Break out and split when one is found.

         Bins are selected for consolidation in a circular fashion
         spanning across malloc calls. This very crudely approximates
         LRU scanning -- it is an effective enough approximation for
         these purposes.

      8. Get space from the system using sbrk.

         Memory is gathered from the system (via sbrk) in a way that
         allows chunks obtained across different sbrk calls to be
         consolidated, but does not require contiguous memory. Thus,
         it should be safe to intersperse mallocs with other sbrk
         calls.

      9. If the selected chunk is too big, then:

         9a If this is the second split request for nb bytes in a row,
             use this chunk to preallocate up to MAX_PREALLOCS
             additional chunks of size nb and place them on the
             returned chunk list.  (Placing them here rather than in
             bins speeds up the most common case where the user
             program requests an uninterrupted series of identically
             sized chunks. If this is not true, the chunks will be
             binned in step 4 next time.)

         9b Split off the remainder and place in last_remainder.
             Because of all the above, the remainder is always a
             `clean' chunk.

     10.  Return the chunk.


  Free: 
    Deallocation (free) consists only of placing the chunk on a list
    of returned chunks. free(0) has no effect.  Because freed chunks
    may be overwritten with link fields, this malloc will often die
    when freed memory is overwritten by user programs.  This can be
    very effective (albeit in an annoying way) in helping users track
    down dangling pointers.

  Realloc:
    Reallocation proceeds in the usual way. If a chunk can be extended,
    it is, else a malloc-copy-free sequence is taken. 

  Memalign, valloc:
    memalign arequests more than enough space from malloc, finds a spot
    within that chunk that meets the alignment request, and then
    possibly frees the leading and trailing space. Overreliance on
    memalign is a sure way to fragment space.

* Other implementation notes

  This malloc is NOT designed to work in multiprocessing applications.
  No semaphores or other concurrency control are provided to ensure
  that multiple malloc or free calls don't run at the same time, which
  could be disasterous. A single semaphore could be used across malloc,
  realloc, and free. It would be hard to obtain finer granularity.

  The implementation is in straight, hand-tuned ANSI C.  Among other
  consequences, it uses a lot of macros. These would be nicer as
  inlinable procedures, but using macros allows use with non-inlining
  compilers, and also makes it a bit easier to control when they
  should be expanded out by selectively embedding them in other macros
  and procedures. (According to profile information, it is almost, but
  not quite always best to expand.)

*/



/* TUNABLE PARAMETERS */

/* 
  SBRK_UNIT is a good power of two to call sbrk with It should
  normally be system page size or a multiple thereof.  If sbrk is very
  slow on a system, it pays to increase this.  Otherwise, it should
  not matter too much.
*/

#define SBRK_UNIT 8192

/* 
  MAX_PREALLOCS is the maximum number of chunks to preallocate.  The
  actual number to prealloc depends on the available space in a
  selected victim, so typical numbers will be less than the maximum.
  Because of this, the exact value seems not to matter too much, at
  least within values from around 1 to 100.  Since preallocation is
  heuristic, making it too huge doesn't help though. It may blindly
  create a lot of chunks when it turns out not to need any more, and
  then consolidate them all back again immediatetly. While this is
  pretty fast, it is better to avoid it.
*/

#define MAX_PREALLOCS 0




/* preliminaries */

#ifndef __STD_C
#ifdef __STDC__
#define	__STD_C		1
#else
#if __cplusplus
#define __STD_C		1
#else
#define __STD_C		0
#endif /*__cplusplus*/
#endif /*__STDC__*/
#endif /*__STD_C*/

#ifndef _BEGIN_EXTERNS_
#if __cplusplus
#define _BEGIN_EXTERNS_	extern "C" {
#define _END_EXTERNS_	}
#else
#define _BEGIN_EXTERNS_
#define _END_EXTERNS_
#endif
#endif /*_BEGIN_EXTERNS_*/

#ifndef _ARG_
#if __STD_C
#define _ARG_(x)	x
#else
#define _ARG_(x)	()
#endif
#endif /*_ARG_*/



#ifndef Void_t
#if __STD_C
#define Void_t		void
#else
#define Void_t		char
#endif
#endif /*Void_t*/

#ifndef NIL
#define NIL(type)	((type)0)
#endif /*NIL*/

#if __STD_C
#include <stddef.h>   /* for size_t */
#else
#include <sys/types.h>
#endif

#include <stdio.h>    /* needed for malloc_stats */

#ifdef __cplusplus
extern "C" {
#endif

#if __STD_C
extern Void_t*     sbrk(size_t);
#else
extern Void_t*     sbrk();
#endif

/* mechanics for getpagesize; adapted from bsd/gnu getpagesize.h */

#if defined(BSD) || defined(DGUX) || defined(sun) || defined(HAVE_GETPAGESIZE)
   extern size_t getpagesize();
#  define malloc_getpagesize getpagesize()
#else
#  include <sys/param.h>
#  ifdef EXEC_PAGESIZE
#    define malloc_getpagesize EXEC_PAGESIZE
#  else
#    ifdef NBPG
#      ifndef CLSIZE
#        define malloc_getpagesize NBPG
#      else
#        define malloc_getpagesize (NBPG * CLSIZE)
#      endif
#    else 
#      ifdef NBPC
#        define malloc_getpagesize NBPC
#      else
#        define malloc_getpagesize SBRK_UNIT    /* just guess */
#      endif
#    endif 
#  endif
#endif 

#ifdef __cplusplus
};  /* end of extern "C" */
#endif



/*  CHUNKS */


struct malloc_chunk
{
  size_t size;               /* Size in bytes, including overhead. */
                             /* Or'ed with INUSE if in use. */

  struct malloc_chunk* fd;   /* double links -- used only if free. */
  struct malloc_chunk* bk;

};

typedef struct malloc_chunk* mchunkptr;

/*  sizes, alignments */

#define SIZE_SZ              (sizeof(size_t))
#define MALLOC_MIN_OVERHEAD  (SIZE_SZ + SIZE_SZ)
#define MALLOC_ALIGN_MASK    (MALLOC_MIN_OVERHEAD - 1)
#define MINSIZE              (sizeof(struct malloc_chunk) + SIZE_SZ)


/* pad request bytes into a usable size */

#define request2size(req) \
  (((long)(req) <= 0) ?  MINSIZE : \
    (((req) + MALLOC_MIN_OVERHEAD + MALLOC_ALIGN_MASK) \
      & ~(MALLOC_ALIGN_MASK)))


/* Check if m has acceptable alignment */

#define aligned_OK(m)    (((size_t)((m)) & (MALLOC_ALIGN_MASK)) == 0)


/* Check if a chunk is immediately usable */

#define exact_fit(ptr, req) ((unsigned)((ptr)->size - (req)) < MINSIZE)

/* maintaining INUSE via size field */

#define INUSE  0x1     /* size field is or'd with INUSE when in use */
                       /* INUSE must be exactly 1, so can coexist with size */

#define inuse(p)       ((p)->size & INUSE)
#define set_inuse(p)   ((p)->size |= INUSE)
#define clear_inuse(p) ((p)->size &= ~INUSE)



/* Physical chunk operations  */

/* Ptr to next physical malloc_chunk. */

#define next_chunk(p)\
  ((mchunkptr)( ((char*)(p)) + ((p)->size & ~INUSE) ))

/* Ptr to previous physical malloc_chunk */

#define prev_chunk(p)\
  ((mchunkptr)( ((char*)(p)) - ( *((size_t*)((char*)(p) - SIZE_SZ)) & ~INUSE)))

/* place size at front and back of chunk */

#define set_size(P, Sz)														  \
{ 																			  \
  size_t Sss = (Sz);														  \
  (P)->size = *((size_t*)((char*)(P) + Sss - SIZE_SZ)) = Sss;				  \
}																			  \


/* conversion from malloc headers to user pointers, and back */

#define chunk2mem(p)   ((Void_t*)((char*)(p) + SIZE_SZ))
#define mem2chunk(mem) ((mchunkptr)((char*)(mem) - SIZE_SZ))




/* BINS */

struct malloc_bin
{
  struct malloc_chunk dhd;   /* dirty list header */
  struct malloc_chunk chd;   /* clean list header */
};

typedef struct malloc_bin* mbinptr;


/* field-extraction macros */

#define clean_head(b)  (&((b)->chd))
#define first_clean(b) ((b)->chd.fd)
#define last_clean(b)  ((b)->chd.bk)

#define dirty_head(b)  (&((b)->dhd))
#define first_dirty(b) ((b)->dhd.fd)
#define last_dirty(b)  ((b)->dhd.bk)




/* The bins, initialized to have null double linked lists */

#define NBINS     128
#define LASTBIN   (&(av[NBINS-1]))
#define FIRSTBIN  (&(av[2]))      /* 1st 2 bins unused but simplify indexing */

/* sizes < MAX_SMALLBIN_SIZE are special-cased since bins are 8 bytes apart */

#define MAX_SMALLBIN_SIZE   512 

/* Helper macro to initialize bins */
#define IAV(i)\
  {{ 0, &(av[i].dhd),  &(av[i].dhd) }, { 0, &(av[i].chd),  &(av[i].chd) }}

static struct malloc_bin  av[NBINS] = 
{
  IAV(0),   IAV(1),   IAV(2),   IAV(3),   IAV(4), 
  IAV(5),   IAV(6),   IAV(7),   IAV(8),   IAV(9),
  IAV(10),  IAV(11),  IAV(12),  IAV(13),  IAV(14), 
  IAV(15),  IAV(16),  IAV(17),  IAV(18),  IAV(19),
  IAV(20),  IAV(21),  IAV(22),  IAV(23),  IAV(24), 
  IAV(25),  IAV(26),  IAV(27),  IAV(28),  IAV(29),
  IAV(30),  IAV(31),  IAV(32),  IAV(33),  IAV(34), 
  IAV(35),  IAV(36),  IAV(37),  IAV(38),  IAV(39),
  IAV(40),  IAV(41),  IAV(42),  IAV(43),  IAV(44), 
  IAV(45),  IAV(46),  IAV(47),  IAV(48),  IAV(49),
  IAV(50),  IAV(51),  IAV(52),  IAV(53),  IAV(54), 
  IAV(55),  IAV(56),  IAV(57),  IAV(58),  IAV(59),
  IAV(60),  IAV(61),  IAV(62),  IAV(63),  IAV(64), 
  IAV(65),  IAV(66),  IAV(67),  IAV(68),  IAV(69),
  IAV(70),  IAV(71),  IAV(72),  IAV(73),  IAV(74), 
  IAV(75),  IAV(76),  IAV(77),  IAV(78),  IAV(79),
  IAV(80),  IAV(81),  IAV(82),  IAV(83),  IAV(84), 
  IAV(85),  IAV(86),  IAV(87),  IAV(88),  IAV(89),
  IAV(90),  IAV(91),  IAV(92),  IAV(93),  IAV(94), 
  IAV(95),  IAV(96),  IAV(97),  IAV(98),  IAV(99),
  IAV(100), IAV(101), IAV(102), IAV(103), IAV(104), 
  IAV(105), IAV(106), IAV(107), IAV(108), IAV(109),
  IAV(110), IAV(111), IAV(112), IAV(113), IAV(114), 
  IAV(115), IAV(116), IAV(117), IAV(118), IAV(119),
  IAV(120), IAV(121), IAV(122), IAV(123), IAV(124), 
  IAV(125), IAV(126), IAV(127)
};



/* 
  Auxiliary lists 

  Even though they use bk/fd ptrs, neither of these are doubly linked!
  They are null-terminated since only the first is ever accessed.
  returned_list is just singly linked for speed in free().
  last_remainder currently has length of at most one.

*/

static mchunkptr returned_list = 0;  /* List of (unbinned) returned chunks */
static mchunkptr last_remainder = 0; /* last remaindered chunk from malloc */



/* 
  Indexing into bins 

  Bins are log-spaced:

  64 bins of size       8
  32 bins of size      64
  16 bins of size     512
   8 bins of size    4096
   4 bins of size   32768
   2 bins of size  262144
   1 bin  of size 2097152
   1 bin  of size what's left

  There is actually a little bit of slop in the numbers in findbin()
  to avoid recentering. This makes no difference elsewhere.
*/


#define findbin(Sizefb, Bfb)												  \
{																			  \
  size_t Sfb = (Sizefb);													  \
  if      (Sfb <     512) (Bfb) = av +        (Sfb >>  3);					  \
  else if (Sfb <    2560) (Bfb) = av + (56  + (Sfb >>  6));					  \
  else if (Sfb <   10752) (Bfb) = av + (91  + (Sfb >>  9));					  \
  else if (Sfb <   43520) (Bfb) = av + (110 + (Sfb >> 12));					  \
  else if (Sfb <  174592) (Bfb) = av + (119 + (Sfb >> 15));					  \
  else if (Sfb <  698880) (Bfb) = av + (124 + (Sfb >> 18));					  \
  else if (Sfb < 2796032) (Bfb) = av +  126;								  \
  else                    (Bfb) = av +  127;								  \
}																			  \

															



/* Keep track of the maximum actually used clean bin, to make loops faster */
/* (Not worth it to do the same for dirty ones) */

static mbinptr maxClean = FIRSTBIN;

#define reset_maxClean														  \
{																			  \
  while (maxClean > FIRSTBIN && clean_head(maxClean)==last_clean(maxClean))	  \
    --maxClean;																  \
}																			  \


/* Macros for linking and unlinking chunks */

/* take a chunk off a list */

#define unlink(Qul)															  \
{																			  \
  mchunkptr Bul = (Qul)->bk;												  \
  mchunkptr Ful = (Qul)->fd;												  \
  Ful->bk = Bul;  Bul->fd = Ful;											  \
}																			  \


/* place a chunk on the dirty list of its bin */

#define dirtylink(Qdl)														  \
{																			  \
  mchunkptr Pdl = (Qdl);													  \
  mbinptr   Bndl; 															  \
  mchunkptr Hdl, Fdl;														  \
																			  \
  findbin(Pdl->size, Bndl);													  \
  Hdl  = dirty_head(Bndl); 													  \
  Fdl  = Hdl->fd; 															  \
																			  \
  Pdl->bk = Hdl;  Pdl->fd = Fdl;  Fdl->bk = Hdl->fd = Pdl;					  \
}																			  \



/* Place a consolidated chunk on a clean list */

#define cleanlink(Qcl)														  \
{																			  \
  mchunkptr Pcl = (Qcl);													  \
  mbinptr Bcl; 																  \
  mchunkptr Hcl, Fcl;														  \
																			  \
  findbin(Qcl->size, Bcl);													  \
  Hcl  = clean_head(Bcl); 													  \
  Fcl  = Hcl->fd; 															  \
  if (Hcl == Fcl && Bcl > maxClean) maxClean = Bcl;							  \
																			  \
  Pcl->bk = Hcl;  Pcl->fd = Fcl;  Fcl->bk = Hcl->fd = Pcl;					  \
}																			  \



/* consolidate one chunk */

#define consolidate(Qc)														  \
{																			  \
  for (;;)																	  \
  {																			  \
    mchunkptr Pc = prev_chunk(Qc);											  \
    if (!inuse(Pc))															  \
    {																		  \
      unlink(Pc);															  \
      set_size(Pc, Pc->size + (Qc)->size);									  \
      (Qc) = Pc;															  \
    }																		  \
    else break;																  \
  }																			  \
  for (;;)																	  \
  {																			  \
    mchunkptr Nc = next_chunk(Qc);											  \
    if (!inuse(Nc))															  \
    {																		  \
      unlink(Nc);															  \
      set_size((Qc), (Qc)->size + Nc->size);								  \
    }																		  \
    else break;																  \
  }																			  \
}																			  \



/* Place a freed chunk on the returned_list */

#define return_chunk(Prc)													  \
{																			  \
  (Prc)->fd = returned_list;												  \
  returned_list = (Prc); 													  \
}																			  \



/* Misc utilities */

/* A helper for realloc */

static void clear_aux_lists()
{
  if (last_remainder != 0)
  {							
    cleanlink(last_remainder);
    last_remainder = 0;			
  }								
  while (returned_list != 0)
  {
    mchunkptr p = returned_list;
    returned_list = p->fd;
    clear_inuse(p);
    dirtylink(p);
  }
}

/* Utilities needed below for memalign */
/* Standard greatest common divisor algorithm */

#if __STD_C
static size_t gcd(size_t a, size_t b)
#else
static size_t gcd(a,b) size_t a; size_t b;
#endif
{
  size_t tmp;
  
  if (b > a)
  {
    tmp = a; a = b; b = tmp;
  }
  for(;;)
  {
    if (b == 0)
      return a;
    else if (b == 1)
      return b;
    else
    {
      tmp = b;
      b = a % b;
      a = tmp;
    }
  }
}

#if __STD_C
static size_t  lcm(size_t x, size_t y)
#else
static size_t  lcm(x, y) size_t x; size_t y;
#endif
{
  return x / gcd(x, y) * y;
}





/* Dealing with sbrk */
/* This is one step of malloc; broken out for simplicity */

static size_t sbrked_mem = 0; /* Keep track of total mem for malloc_stats */

#if __STD_C
static mchunkptr malloc_from_sys(size_t nb)
#else
static mchunkptr malloc_from_sys(nb) size_t nb;
#endif
{

  /* The end of memory returned from previous sbrk call */
  static size_t* last_sbrk_end = 0; 

  mchunkptr p;            /* Will hold a usable chunk */
  size_t*   ip;           /* to traverse sbrk ptr in size_t units */
  char*     cp;           /* result of sbrk call */
  
  /* Find a good size to ask sbrk for.  */
  /* Minimally, we need to pad with enough space */
  /* to place dummy size/use fields to ends if needed */

  size_t sbrk_size = ((nb + SBRK_UNIT - 1 + SIZE_SZ + SIZE_SZ) 
                       / SBRK_UNIT) * SBRK_UNIT;

  cp = (char*)(sbrk(sbrk_size));
  if (cp == (char*)(-1)) /* sbrk returns -1 on failure */
    return 0;

  ip = (size_t*)cp;
  sbrked_mem += sbrk_size;

  if (last_sbrk_end != &ip[-1]) /* Is this chunk continguous with last? */
  {                             
    /* It's either first time through or someone else called sbrk. */
    /* Arrange end-markers at front & back */

    /* Shouldn't be necessary, but better to be safe */
    while (!aligned_OK(ip)) { ++ip; sbrk_size -= SIZE_SZ; }

    /* Mark the front as in use to prevent merging. (End done below.) */
    /* Note we can get away with only 1 word, not MINSIZE overhead here */

    *ip++ = SIZE_SZ | INUSE;
    
    p = (mchunkptr)ip;
    set_size(p,sbrk_size - (SIZE_SZ + SIZE_SZ)); 
    
  }
  else 
  {
    mchunkptr l;  

    /* We can safely make the header start at end of prev sbrked chunk. */
    /* We will still have space left at the end from a previous call */
    /* to place the end marker, below */

    p = (mchunkptr)(last_sbrk_end);
    set_size(p, sbrk_size);

    /* Even better, maybe we can merge with last fragment: */

    l = prev_chunk(p);
    if (!inuse(l))  
    {
      unlink(l);
      set_size(l, p->size + l->size);
      p = l;
    }

  }

  /* mark the end of sbrked space as in use to prevent merging */

  last_sbrk_end = (size_t*)((char*)p + p->size);
  *last_sbrk_end = SIZE_SZ | INUSE;

  return p;
}




#if __STD_C
Void_t* malloc(size_t bytes)
#else
Void_t* malloc(bytes) size_t bytes;
#endif
{
  static mbinptr rover = LASTBIN;      /* Circular roving ptr */
  static size_t previous_request = 0;  /* To control preallocation */

  size_t    nb = request2size(bytes);  /* padded request size */
  mbinptr   bin;                       /* corresponding bin */
  mchunkptr victim;                    /* will hold selected chunk */

  /* ----------- Peek (twice) at returned_list; hope for luck */

  if ((victim = returned_list) != 0)
  {
    if (exact_fit(victim, nb)) /* size check works even though INUSE set */
    {
      returned_list = victim->fd;
      return chunk2mem(victim);
    }
    else if ( (victim = victim->fd) != 0 && exact_fit(victim, nb))
    {
      returned_list->fd = victim->fd;
      return chunk2mem(victim);
    }
  }

  findbin(nb, bin);

  /* ---------- Scan own dirty bin */

  if (nb < MAX_SMALLBIN_SIZE - 8)
  {
    /* Small bins special-cased since no size check or traversal needed. */
    /* Also because of MINSIZE slop, next bin is exact fit too */

    mbinptr nextbin = bin + 1;
    if ( ((victim = first_dirty(bin)) != dirty_head(bin)) ||
         ((victim = first_dirty(nextbin)) != dirty_head(nextbin)))
    {
      unlink(victim);
      set_inuse(victim);
      return chunk2mem(victim);
    }
  }
  else
  {
    for (victim=first_dirty(bin); victim != dirty_head(bin); victim=victim->fd)
    {
      if (exact_fit(victim, nb))     /* Can use exact matches only here */
      {
        unlink(victim);
        set_inuse(victim);
        return chunk2mem(victim);
      }
    }
  }


  /* ------------ Search free list, placing unusable chunks in their bins  */

  if ( (victim = returned_list) != 0)
  {
    do
    {
      mchunkptr next = victim->fd;
      if (exact_fit(victim, nb))
      {
        returned_list = next;
        return chunk2mem(victim);
      }
      else 
      {
        clear_inuse(victim);
        dirtylink(victim);
        victim = next;
      }
    } while (victim != 0);
    returned_list = 0;
  }

  /* -------------- Try the remainder from last successful split */

  if ( (victim = last_remainder) != 0)
  {
    last_remainder = 0;
    if (victim->size >= nb)
      goto split;
    else
      cleanlink(victim);
  }



  /* -------------- Try clean bins  */

  if (bin < maxClean)
  {
    mbinptr b;

    /* -------------- Own bin -- need to traverse & size check */
    for (victim=last_clean(bin); victim!=clean_head(bin); victim=victim->bk)
    {
      if (victim->size >= nb)
      {
        unlink(victim); 
        goto split;
      }
    }

    /* -------------- Bigger bins  -- no traversal or size check */

    for (b = bin + 1; b <= maxClean; ++b)
    {
      if ( (victim = last_clean(b)) != clean_head(b) ) 
      {
        unlink(victim);
        goto split;
      }
    }

  }

  reset_maxClean;       /* reset for next time */

  /* -------------- Sweep though other dirty bins */

  {
    mbinptr origin = rover;     /* Start where left off last time. */
    mbinptr b      = rover;
    do
    {
      while ( (victim = last_dirty(b)) != dirty_head(b))
      {
        unlink(victim);
        consolidate(victim);

        if (victim->size >= nb)
        {
          rover = b;
          goto split;
        }
        else
          cleanlink(victim);
      }

      b = (b == FIRSTBIN)? LASTBIN : b - 1;      /* circularly sweep down */

    } while (b != origin);

  }

  /* -------------  Nothing available; get some from sys */

  if ( (victim = malloc_from_sys(nb)) == 0)
    return 0;   /* propagate failure */


  /* -------------- Possibly split victim chunk */

 split:  
  {
    size_t room = victim->size - nb;
    if (room >= MINSIZE)     
    {
      mchunkptr v = victim;  /* Hold so can break up in prealloc */
      
      set_size(victim, nb);  /* Adjust size of chunk to be returned */
      
      /* ---------- Preallocate more if this size  same as last (split) req */
      
      if (previous_request == nb)
      {
        int i;
        
        for (i = 0; i < MAX_PREALLOCS && room >= nb + MINSIZE; ++i)
        {
          room -= nb;
           
          v = (mchunkptr)((char*)(v) + nb); 
          set_size(v, nb);
          set_inuse(v);     /* free-list chunks must have inuse set */
          return_chunk(v);  /* add to free list */
        } 
      }
      else
        previous_request = nb;  /* record for next time */

      /* ---------- Create remainder chunk  */

      last_remainder = (mchunkptr)((char*)(v) + nb);
      set_size(last_remainder, room);
    }

    set_inuse(victim);
    return chunk2mem(victim);
  }
}




#if __STD_C
void free(Void_t* mem)
#else
void free(mem) Void_t* mem;
#endif
{
  if (mem != 0)
  {
    mchunkptr p = mem2chunk(mem);
    return_chunk(p);
  }
}

 


#if __STD_C
Void_t* realloc(Void_t* mem, size_t bytes)
#else
Void_t* realloc(mem, bytes) Void_t* mem; size_t bytes;
#endif
{
  if (mem == 0) 
    return malloc(bytes);
  else
  {
    size_t       nb      = request2size(bytes);
    mchunkptr    p       = mem2chunk(mem);
    size_t       oldsize;
    long         room;
    mchunkptr    nxt;

    if (p == returned_list) /* support realloc-last-freed-chunk idiocy */
       returned_list = returned_list->fd;

    clear_inuse(p);
    oldsize = p->size;

    /* try to expand (even if already big enough), to clean up chunk */

    clear_aux_lists(); /* make freed chunks available to consolidate */

    while (!inuse(nxt = next_chunk(p))) /* Expand the chunk forward */
    {
      unlink(nxt);
      set_size(p, p->size + nxt->size);
    }

    room = p->size - nb;
    if (room >= 0)          /* Successful expansion */
    {
      if (room >= MINSIZE)  /* give some back if possible */
      {
        mchunkptr remainder = (mchunkptr)((char*)(p) + nb);
        set_size(remainder, room);
        cleanlink(remainder);
        set_size(p, nb);
      }
      set_inuse(p);
      return chunk2mem(p);
    }
    else /* Could not expand. Get another chunk and copy. */
    {
      Void_t* newmem;
      size_t count;
      size_t* src;
      size_t* dst;

      set_inuse(p);    /* don't let malloc consolidate us yet! */
      newmem = malloc(nb);

      if (newmem != 0) {
        /* Copy -- we know that alignment is at least `size_t' */
        src = (size_t*) mem;
        dst = (size_t*) newmem;
        count = (oldsize - SIZE_SZ) / sizeof(size_t);
        while (count-- > 0) *dst++ = *src++;
      }

      free(mem);
      return newmem;
    }
  }
}



/* Return a pointer to space with at least the alignment requested */
/* Alignment argument should be a power of two */

#if __STD_C
Void_t* memalign(size_t alignment, size_t bytes)
#else
Void_t* memalign(alignment, bytes) size_t alignment; size_t bytes;
#endif
{
  mchunkptr p;
  size_t    nb = request2size(bytes);
  size_t    room;

  /* find an alignment that both we and the user can live with: */
  /* least common multiple guarantees mutual happiness */
  size_t    align = lcm(alignment, MALLOC_MIN_OVERHEAD);

  /* call malloc with worst case padding to hit alignment; */
  /* we will give back extra */

  size_t req = nb + align + MINSIZE;
  Void_t*  m = malloc(req);

  if (m == 0) return 0; /* propagate failure */

  p = mem2chunk(m);
  clear_inuse(p);


  if (((size_t)(m) % align) != 0) /* misaligned */
  {

    /* find an aligned spot inside chunk */

    mchunkptr ap = (mchunkptr)((((size_t)(m) + align-1) & -align) - SIZE_SZ);

    size_t gap = (size_t)(ap) - (size_t)(p);

    /* we need to give back leading space in a chunk of at least MINSIZE */

    if (gap < MINSIZE)
    {
      /* This works since align >= MINSIZE */
      /* and we've malloc'd enough total room */

      ap = (mchunkptr)( (size_t)(ap) + align );
      gap += align;    
    }

    room = p->size - gap;

    /* give back leader */
    set_size(p, gap);
    dirtylink(p); /* Don't really know if clean or dirty; be safe */

    /* use the rest */
    p = ap;
    set_size(p, room);
  }

  /* also give back spare room at the end */

  room = p->size - nb;
  if (room >= MINSIZE)
  {
    mchunkptr remainder = (mchunkptr)((char*)(p) + nb);
    set_size(remainder, room);
    dirtylink(remainder); /* Don't really know; be safe */
    set_size(p, nb);
  }

  set_inuse(p);
  return chunk2mem(p);

}



/* Derivatives */

#if __STD_C
Void_t* valloc(size_t bytes)
#else
Void_t* valloc(bytes) size_t bytes;
#endif
{
  /* Cache result of getpagesize */
  static size_t malloc_pagesize = 0;

  if (malloc_pagesize == 0) malloc_pagesize = malloc_getpagesize;
  return memalign (malloc_pagesize, bytes);
}


#if __STD_C
Void_t* calloc(size_t n, size_t elem_size)
#else
Void_t* calloc(n, elem_size) size_t n; size_t elem_size;
#endif
{
  size_t sz = n * elem_size;
  Void_t* p = malloc(sz);
  char* q = (char*) p;
  while (sz-- > 0) *q++ = 0;
  return p;
}

#if __STD_C
void cfree(Void_t *mem)
#else
void cfree(mem) Void_t *mem;
#endif
{
  free(mem);
}

#if __STD_C
size_t malloc_usable_size(Void_t* mem)
#else
size_t malloc_usable_size(mem) Void_t* mem;
#endif
{
  if (mem == 0)
    return 0;
  else
  {
    mchunkptr p = (mchunkptr)((char*)(mem) - SIZE_SZ); 
    size_t sz = p->size & ~(INUSE);
    /* report zero if not in use or detectably corrupt */
    if (p->size == sz || sz != *((size_t*)((char*)(p) + sz - SIZE_SZ)))
      return 0;
    else
      return sz - MALLOC_MIN_OVERHEAD;
  }
}
    

void malloc_stats()
{

  /* Traverse through and count all sizes of all chunks */

  size_t avail = 0;
  size_t malloced_mem;

  mbinptr b;

  clear_aux_lists();

  for (b = FIRSTBIN; b <= LASTBIN; ++b)
  {
    mchunkptr p;

    for (p = first_dirty(b); p != dirty_head(b); p = p->fd)
      avail += p->size;

    for (p = first_clean(b); p != clean_head(b); p = p->fd)
      avail += p->size;
  }

  malloced_mem = sbrked_mem - avail;

  fprintf(stderr, "total mem = %10u\n", sbrked_mem);
  fprintf(stderr, "in use    = %10u\n", malloced_mem);

}


