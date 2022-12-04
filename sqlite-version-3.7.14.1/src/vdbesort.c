/*
** 2011 July 9
**
** The author disclaims copyright to this source code.  In place of
** a legal notice, here is a blessing:
**
**    May you do good and not evil.
**    May you find forgiveness for yourself and forgive others.
**    May you share freely, never taking more than you give.
**
*************************************************************************
** This file contains code for the VdbeSorter object, used in concert with
** a VdbeCursor to sort large numbers of keys (as may be required, for
** example, by CREATE INDEX statements on tables too large to fit in main
** memory).
*/

#include "sqliteInt.h"
#include "vdbeInt.h"

#ifndef SQLITE_OMIT_MERGE_SORT

typedef struct VdbeSorterIter VdbeSorterIter;
typedef struct SorterRecord SorterRecord;
typedef struct FileWriter FileWriter;

/*
** NOTES ON DATA STRUCTURE USED FOR N-WAY MERGES:
**
** 这里的思想比较类似于N路归并排序
** As keys are added to the sorter, they are written to disk in a series
** of sorted packed-memory-arrays (PMAs). The size of each PMA is roughly
** the same as the cache-size allowed for temporary databases. In order
** to allow the caller to extract keys from the sorter in sorted order,
** all PMAs currently stored on disk must be merged together. This comment
** describes the data structure used to do so. The structure supports
** merging any number of arrays in a single pass with no redundant comparison
** operations.
** 当键被加入sorter时,它们会以一种排序的PMA(packed-memory-arrays)的形式写入磁盘.每一个PMA的大小
** 大致与临时数据库允许的缓存大小相同.为了能使调用者从sorter中按照排序顺序获取key,当前所有的存储在磁盘上的
** PMA必须合并在一起.这个结构描述了执行此操作的数据结构.此结构支持一次性合并任意数量的array,并且
** 没有多余的比较操作.
**
** The aIter[] array contains an iterator for each of the PMAs being merged.
** An aIter[] iterator either points to a valid key or else is at EOF. For
** the purposes of the paragraphs below, we assume that the array is actually
** N elements in size, where N is the smallest power of 2 greater to or equal
** to the number of iterators being merged. The extra aIter[] elements are
** treated as if they are empty (always at EOF).
** aIter[]数组包含了为合并所有PMA所准备的迭代器(应该是有多少需要合并的PMA,就有多少迭代器,数组alter[]中的元素
** 就是一个个迭代器),一个aIter[]迭代器要么指向一个有效的key,要么在EOF处.
** 我们假定数组实际包含N个元素.(每一个迭代器每一次迭代都可以产生一个元素,迭代器产生的元素是有序的.)
**
** The aTree[] array is also N elements in size. The value of N is stored in
** the VdbeSorter.nTree variable.
** aTree[]数组也是N个元素,N存储在VdbeSorter.nTree变量中.
**
** The final (N/2) elements of aTree[] contain the results of comparing
** pairs of iterator keys together. Element i contains the result of
** comparing aIter[2*i-N] and aIter[2*i-N+1]. Whichever key is smaller, the
** aTree element is set to the index of it.
** aTree[]中的最后N/2个元素,包含了迭代器键比较对的结果.第i个元素包含了aIter[2*i-N]与aIter[2*i-N+1]的比较结果.
** aIter[2*i-N]和aIter[2*i-N+1]二者中,谁更小,就把谁的下标存储在数组aTree[]的第i个元素中. (二路归并)
**
** For the purposes of this comparison, EOF is considered greater than any
** other key value. If the keys are equal (only possible with two EOF
** values), it doesn't matter which index is stored.
** 出于比较的目的,EOF被认为比任何其他key值都要大,如果key相等(仅有可能有两个EOF值),存储哪一个下标并不重要.
**
** The (N/4) elements of aTree[] that preceed the final (N/2) described
** above contains the index of the smallest of each block of 4 iterators.
** And so on. So that aTree[1] contains the index of the iterator that
** currently points to the smallest key value. aTree[0] is unused.
** 数组aTree[]中有(N/4)个元素存的是每四个迭代器所组成的一组中,key值最小的那个迭代器的下标(已经有N/2个元素记录的是两个
** 迭代器两两比较的结果, 对这N/2个元素再次来两两比较,就有N/4个元素记录的是4个迭代器比较的结果,依此类推...)
** 所以aTree[1]中存有指向最小key值的迭代器的下标.aTree[0]没有被使用
**
** Example:
**
**     aIter[0] -> Banana
**     aIter[1] -> Feijoa
**     aIter[2] -> Elderberry
**     aIter[3] -> Currant
**     aIter[4] -> Grapefruit
**     aIter[5] -> Apple
**     aIter[6] -> Durian
**     aIter[7] -> EOF
**
**     aTree[] = { X, 5   0, 5    0, 3, 5, 6 }
** aTree有8个元素,因此N==8
** 结果说明,aIter[0]与aIter[1]比较,aIter[0]比较小,结果放入aTree[4] --> 0
**        aIter[2]与aIter[3]比较,aIter[3]比较小,结果放入aTree[5] --> 3
**         ...
**        aTree[2]是aIter[0]与aIter[3]的比较结果,因此aTree[2] --> 0
**        aTree[3]是aIter[5]与aIter[6]的比较结果,因此aTree[3] --> 5
**        aTree[1]是aIter[0]与aIter[5]的比较结果,因此aTree[1] --> 5
**  妥妥的二路归并排序
**
** The current element is "Apple" (the value of the key indicated by
** iterator 5). When the Next() operation is invoked, iterator 5 will
** be advanced to the next key in its segment. Say the next key is
** "Eggplant":
** 当前的元素为Apple(key的值为5),当调用Next()方法,迭代器5将会迁移到下一个key,假定下一个key为Eggplant
** 则有:
**
**     aIter[5] -> Eggplant
**
** The contents of aTree[] are updated first by comparing the new iterator
** 5 key to the current key of iterator 4 (still "Grapefruit"). The iterator
** 5 value is still smaller, so aTree[6] is set to 5. And so on up the tree.
** The value of iterator 6 - "Durian" - is now smaller than that of iterator
** 5, so aTree[3] is set to 6. Key 0 is smaller than key 6 (Banana<Durian),
** so the value written into element 1 of the array is 0. As follows:
** 首先,比较迭代器5所指向的key以及迭代器所指向的key(迭代器4指向Grapefruit),这时,迭代器5所指向的key仍然更小.
** 所以aTree[6]的值被设置为5, 迭代器6所指向的key值Durian比迭代器5所指向的key值(Eggplant)小,所以aTree[3]
** 被设置为6,而迭代器0所指向的keyBanana又比迭代器6所指向的key值小,即Banana<Durian,所以aTree[1]被设置成0,
** 结果如下:
**
**     aTree[] = { X, 0   0, 6    0, 3, 5, 6 }
** 也就是说,迭代器变动一次,会导致重新排序.
**
** In other words, each time we advance to the next sorter element, log2(N)
** key comparison operations are required, where N is the number of segments
** being merged (rounded up to the next power of 2).
** 也就是说,我们每次前进到sorter的下一个元素时,需要做log2(N)次key比较,这里的N是要合并的段的个数.
*/
struct VdbeSorter
{
    /* 当前pTemp1文件的写偏移 */
    i64 iWriteOff;                  /* Current write offset within file pTemp1 */
    /* 当前pTemp1文件的读偏移 */
    i64 iReadOff;                   /* Current read offset within file pTemp1 */
    int nInMemory;                  /* Current size of pRecord list as PMA */
    int nTree;                      /* Used size of aTree/aIter (power of 2) */
    /* 存储在pTemp1文件中的PMA的个数 */
    int nPMA;                       /* Number of PMAs stored in pTemp1 */
    int mnPmaSize;                  /* Minimum PMA size, in bytes */
    int mxPmaSize;                  /* Maximum PMA size, in bytes.  0==no limit */
    /* 要合并的迭代器构成的数组 */
    VdbeSorterIter *aIter;          /* Array of iterators to merge */
    int *aTree;                     /* Current state of incremental merge */
    sqlite3_file *pTemp1;           /* PMA file 1 */
    SorterRecord *pRecord;          /* Head of in-memory record list */
    UnpackedRecord *pUnpacked;      /* Used to unpack keys */
};

/*
** The following type is an iterator for a PMA. It caches the current key in
** variables nKey/aKey. If the iterator is at EOF, pFile==0.
** PMA的一个迭代器,它缓存着当前的key
*/
struct VdbeSorterIter
{
    /* 这个变量用于表示偏移,指示aBuffer数组中的哪一个位置 */
    i64 iReadOff;                   /* Current read offset */
    /* 此变量位于EOF后面,距离EOF一个字节 */
    i64 iEof;                       /* 1 byte past EOF for this iterator */
    /* aAlloc分配的字节数 */
    int nAlloc;                     /* Bytes of space at aAlloc */
    int nKey;                       /* Number of bytes in key */
    sqlite3_file *pFile;            /* File iterator is reading from */
    u8 *aAlloc;                     /* Allocated space */
    u8 *aKey;                       /* Pointer to current key */
    u8 *aBuffer;                    /* Current read buffer */
    int nBuffer;                    /* Size of read buffer in bytes */
};

/*
** An instance of this structure is used to organize the stream of records
** being written to files by the merge-sort code into aligned, page-sized
** blocks.  Doing all I/O in aligned page-sized blocks helps I/O to go
** faster on many operating systems.
** 本结构体的一个实例用于组织record流,将record写入对齐的,页大小的块中
*/
struct FileWriter
{
    /* 处于错误状态时,值非零 */
    int eFWErr;                     /* Non-zero if in an error state */
    /* 指向写缓存的指针 */
    u8 *aBuffer;                    /* Pointer to write buffer */
    int nBuffer;                    /* Size of write buffer in bytes */
    /* 要写入缓存的第一个字节 */
    int iBufStart;                  /* First byte of buffer to write */
    /* 要写入缓存的最后一个字节 */
    int iBufEnd;                    /* Last byte of buffer to write */
    i64 iWriteOff;                  /* Offset of start of buffer in file */
    sqlite3_file *pFile;            /* File to write to */
};

/*
** A structure to store a single record. All in-memory records are connected
** together into a linked list headed at VdbeSorter.pRecord using the
** SorterRecord.pNext pointer.
** 用于存储单条记录的结果,所有内存中的记录都被连接在一起,构成链表,链表头部位VdbeSorter.pRecord
*/
struct SorterRecord
{
    void *pVal;
    int nVal;
    SorterRecord *pNext;
};

/* Minimum allowable value for the VdbeSorter.nWorking variable */
#define SORTER_MIN_WORKING 10

/* Maximum number of segments to merge in a single pass. */
#define SORTER_MAX_MERGE_COUNT 16

/*
** Free all memory belonging to the VdbeSorterIter object passed as the second
** argument. All structure fields are set to zero before returning.
*/
static void vdbeSorterIterZero(sqlite3 *db, VdbeSorterIter *pIter)
{
    sqlite3DbFree(db, pIter->aAlloc);
    sqlite3DbFree(db, pIter->aBuffer);
    memset(pIter, 0, sizeof(VdbeSorterIter));
}

/*
** Read nByte bytes of data from the stream of data iterated by object p.
** If successful, set *ppOut to point to a buffer containing the data
** and return SQLITE_OK. Otherwise, if an error occurs, return an SQLite
** error code.
** 从数据流中读取nByte字节的数据,如果成功,将*ppOut指向为一个包含数据的缓冲器,并且返回SQLITE_OK.
** 否则,如果发生错误,返回SQLite的错误码.
**
** The buffer indicated by *ppOut may only be considered valid until the
** next call to this function.
** *ppOut指向的缓冲区会一直有效,直到下一次调用此函数
** @param nByte 要读取的字节数
** @param **ppout 指向缓冲区
*/
static int vdbeSorterIterRead(
    sqlite3 *db,                    /* Database handle (for malloc) */
    VdbeSorterIter *p,              /* Iterator */
    int nByte,                      /* Bytes of data to read */
    u8 **ppOut                      /* OUT: Pointer to buffer containing data */
)
{
    int iBuf;                       /* Offset within buffer to read from */
    int nAvail;                     /* Bytes of data available in buffer */
    assert(p->aBuffer);

    /* If there is no more data to be read from the buffer, read the next
    ** p->nBuffer bytes of data from the file into it. Or, if there are less
    ** than p->nBuffer bytes remaining in the PMA, read all remaining data.  */
    /* 如果缓冲区中已经没有数据可读,从文件中读取p->nBuffer个字节的数据到缓冲区中,如果缓冲区中还有残留数据
    ** 那么要将剩余的缓冲区读满.
    */
    iBuf = p->iReadOff % p->nBuffer;
    if (iBuf == 0)
    {
        int nRead;                    /* Bytes to read from disk */
        int rc;                       /* sqlite3OsRead() return code */

        /* Determine how many bytes of data to read. */
        nRead = (int)(p->iEof - p->iReadOff);
        if (nRead > p->nBuffer) nRead = p->nBuffer;
        assert(nRead > 0);

        /* Read data from the file. Return early if an error occurs. */
        /* 读取nRead个字节的数据 */
        rc = sqlite3OsRead(p->pFile, p->aBuffer, nRead, p->iReadOff);
        assert(rc != SQLITE_IOERR_SHORT_READ);
        if (rc != SQLITE_OK) return rc;
    }
    nAvail = p->nBuffer - iBuf; /* 缓冲区中未读取完的字节的数目 */

    if (nByte <= nAvail)
    {
        /* The requested data is available in the in-memory buffer. In this
        ** case there is no need to make a copy of the data, just return a
        ** pointer into the buffer to the caller.  */
        *ppOut = &p->aBuffer[iBuf];
        p->iReadOff += nByte; /* 缓冲区中已经拥有足够的数据 */
    }
    else
    {
        /* The requested data is not all available in the in-memory buffer.
        ** In this case, allocate space at p->aAlloc[] to copy the requested
        ** range into. Then return a copy of pointer p->aAlloc to the caller.  */
        int nRem;                     /* Bytes remaining to copy */

        /* Extend the p->aAlloc[] allocation if required. */
        if (p->nAlloc < nByte)
        {
            int nNew = p->nAlloc * 2;
            while (nByte > nNew) nNew = nNew * 2;
            p->aAlloc = sqlite3DbReallocOrFree(db, p->aAlloc, nNew);
            if (!p->aAlloc) return SQLITE_NOMEM;
            p->nAlloc = nNew;
        }

        /* Copy as much data as is available in the buffer into the start of
        ** p->aAlloc[].  */
        memcpy(p->aAlloc, &p->aBuffer[iBuf], nAvail); /* 将未读完的数据拷贝到首部 */
        p->iReadOff += nAvail;
        nRem = nByte - nAvail;

        /* The following loop copies up to p->nBuffer bytes per iteration into
        ** the p->aAlloc[] buffer.  */
        while (nRem > 0)
        {
            int rc;                     /* vdbeSorterIterRead() return code */
            int nCopy;                  /* Number of bytes to copy */
            u8 *aNext;                  /* Pointer to buffer to copy data from */

            nCopy = nRem;
            if (nRem > p->nBuffer) nCopy = p->nBuffer;
            rc = vdbeSorterIterRead(db, p, nCopy, &aNext);
            if (rc != SQLITE_OK) return rc;
            assert(aNext != p->aAlloc);
            memcpy(&p->aAlloc[nByte - nRem], aNext, nCopy);
            nRem -= nCopy;
        }

        *ppOut = p->aAlloc;
    }

    return SQLITE_OK;
}

/*
** Read a varint from the stream of data accessed by p. Set *pnOut to
** the value read.
** 从数据流中读取一个可变大小的int(varint), 将*pnOut设置为读取到的数据
*/
static int vdbeSorterIterVarint(sqlite3 *db, VdbeSorterIter *p, u64 *pnOut)
{
    int iBuf;

    iBuf = p->iReadOff % p->nBuffer;
    if (iBuf && (p->nBuffer - iBuf) >= 9) /* 长度足够存储一个varint */
    {
        p->iReadOff += sqlite3GetVarint(&p->aBuffer[iBuf], pnOut);
    }
    else
    {
        u8 aVarint[16], *a;
        int i = 0, rc;
        do
        {
            rc = vdbeSorterIterRead(db, p, 1, &a);
            if (rc) return rc;
            aVarint[(i++) & 0xf] = a[0];
        }
        while ((a[0] & 0x80) != 0);
        sqlite3GetVarint(aVarint, pnOut);
    }

    return SQLITE_OK;
}


/*
** Advance iterator pIter to the next key in its PMA. Return SQLITE_OK if
** no error occurs, or an SQLite error code if one does.
** 移动迭代器到下一个元素
*/
static int vdbeSorterIterNext(
    sqlite3 *db,                    /* Database handle (for sqlite3DbMalloc() ) */
    VdbeSorterIter *pIter           /* Iterator to advance */
)
{
    int rc;                         /* Return Code */
    u64 nRec = 0;                   /* Size of record in bytes */

    if (pIter->iReadOff >= pIter->iEof)
    {
        /* This is an EOF condition */
        vdbeSorterIterZero(db, pIter); /* 迭代器迭代完毕 */
        return SQLITE_OK;
    }

    rc = vdbeSorterIterVarint(db, pIter, &nRec);
    if (rc == SQLITE_OK)
    {
        pIter->nKey = (int)nRec; /* key所占的字节数目 */
        rc = vdbeSorterIterRead(db, pIter, (int)nRec, &pIter->aKey); /* 读取key的值 */
    }

    return rc;
}

/*
** Initialize iterator pIter to scan through the PMA stored in file pFile
** starting at offset iStart and ending at offset iEof-1. This function
** leaves the iterator pointing to the first key in the PMA (or EOF if the
** PMA is empty).
** 初始化一个迭代器pIter,用来扫描存储在文件pFile中的PMA, 文件偏移量为iStart,结尾为iEof-1
** 此函数让迭代器指向第一个key.
*/
static int vdbeSorterIterInit(
    sqlite3 *db,                    /* Database handle */
    const VdbeSorter *pSorter,      /* Sorter object */
    /* 从文件的偏移量iStart处开始读取 */
    i64 iStart,                     /* Start offset in pFile */
    VdbeSorterIter *pIter,          /* Iterator to populate */
    i64 *pnByte                     /* IN/OUT: Increment this value by PMA size */
)
{
    int rc = SQLITE_OK;
    int nBuf;

    nBuf = sqlite3BtreeGetPageSize(db->aDb[0].pBt);

    assert(pSorter->iWriteOff > iStart);
    assert(pIter->aAlloc == 0);
    assert(pIter->aBuffer == 0);
    pIter->pFile = pSorter->pTemp1;
    pIter->iReadOff = iStart; /* 记录下偏移量 */
    pIter->nAlloc = 128;
    pIter->aAlloc = (u8 *)sqlite3DbMallocRaw(db, pIter->nAlloc); /* 128字节的缓冲区 */
    pIter->nBuffer = nBuf;
    pIter->aBuffer = (u8 *)sqlite3DbMallocRaw(db, nBuf);

    if (!pIter->aBuffer)
    {
        rc = SQLITE_NOMEM;
    }
    else
    {
        int iBuf;

        iBuf = iStart % nBuf;
        if (iBuf)
        {
            int nRead = nBuf - iBuf;
            if ((iStart + nRead) > pSorter->iWriteOff)
            {
                nRead = (int)(pSorter->iWriteOff - iStart);
            }
            rc = sqlite3OsRead(
                     pSorter->pTemp1, &pIter->aBuffer[iBuf], nRead, iStart
                 );
            assert(rc != SQLITE_IOERR_SHORT_READ);
        }

        if (rc == SQLITE_OK)
        {
            u64 nByte;                       /* Size of PMA in bytes */
            pIter->iEof = pSorter->iWriteOff;
            rc = vdbeSorterIterVarint(db, pIter, &nByte);
            pIter->iEof = pIter->iReadOff + nByte;
            *pnByte += nByte;
        }
    }

    if (rc == SQLITE_OK)
    {
        rc = vdbeSorterIterNext(db, pIter);
    }
    return rc;
}


/*
** Compare key1 (buffer pKey1, size nKey1 bytes) with key2 (buffer pKey2,
** size nKey2 bytes).  Argument pKeyInfo supplies the collation functions
** used by the comparison. If an error occurs, return an SQLite error code.
** Otherwise, return SQLITE_OK and set *pRes to a negative, zero or positive
** value, depending on whether key1 is smaller, equal to or larger than key2.
** key1(buffer pKey1, size nKey1 bytes)与key2(buffer pKey2, size nKey2 bytes)进行比较.
** 参数pKeyInfo提交整理函数,用来比较,如果错误发生,返回错误码.否则返回SQLITE_OK,并将*pRes设置为比较结果.
** 负数,表示小于,正数表示大于,0表示相等.
**
** If the bOmitRowid argument is non-zero, assume both keys end in a rowid
** field. For the purposes of the comparison, ignore it. Also, if bOmitRowid
** is true and key1 contains even a single NULL value, it is considered to
** be less than key2. Even if key2 also contains NULL values.
** 如果bOmitRowid参数为非零,假定keys以一个rowid结尾.出于比较的目的,忽略它,如果bOmitRowid为true
** 并且key1包含了只有单个NULL值,
**
** If pKey2 is passed a NULL pointer, then it is assumed that the pCsr->aSpace
** has been allocated and contains an unpacked record that is used as key2.
** 如果pKey2为NULL,那么pCsr->aSpace肯定不为空,而且包含了一个unpacked record,那么将其作为key2
*/
static void vdbeSorterCompare(
    const VdbeCursor *pCsr,         /* Cursor object (for pKeyInfo) */
    int bOmitRowid,                 /* Ignore rowid field at end of keys */
    const void *pKey1, int nKey1,   /* Left side of comparison */
    const void *pKey2, int nKey2,   /* Right side of comparison */
    int *pRes                       /* OUT: Result of comparison */
)
{
    KeyInfo *pKeyInfo = pCsr->pKeyInfo;
    VdbeSorter *pSorter = pCsr->pSorter;
    UnpackedRecord *r2 = pSorter->pUnpacked;
    int i;

    if (pKey2)
    {
        sqlite3VdbeRecordUnpack(pKeyInfo, nKey2, pKey2, r2);
    }

    if (bOmitRowid) /* 忽略rowid */
    {
        r2->nField = pKeyInfo->nField;
        assert(r2->nField > 0);
        for (i = 0; i < r2->nField; i++)
        {
            if (r2->aMem[i].flags & MEM_Null) /* 有一个字段为NULL */
            {
                *pRes = -1;
                return;
            }
        }
        r2->flags |= UNPACKED_PREFIX_MATCH;
    }

    *pRes = sqlite3VdbeRecordCompare(nKey1, pKey1, r2);
}

/*
** This function is called to compare two iterator keys when merging
** multiple b-tree segments. Parameter iOut is the index of the aTree[]
** value to recalculate.
** 此函数用于比较两个迭代器的key,当合并多个b-tree segments的时候,参数iOut是aTree[]数组
** 的下标
*/
static int vdbeSorterDoCompare(const VdbeCursor *pCsr, int iOut)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    int i1;
    int i2;
    int iRes;
    VdbeSorterIter *p1;
    VdbeSorterIter *p2;

    assert(iOut < pSorter->nTree && iOut > 0);
    /* i1以及i2表示要比较的两个迭代器 */

    if (iOut >= (pSorter->nTree / 2))
    {
        i1 = (iOut - pSorter->nTree / 2) * 2;
        i2 = i1 + 1;
    }
    else /* 二路合并 */
    {
        i1 = pSorter->aTree[iOut * 2];
        i2 = pSorter->aTree[iOut * 2 + 1];
    }

    p1 = &pSorter->aIter[i1];
    p2 = &pSorter->aIter[i2];

    if (p1->pFile == 0)
    {
        iRes = i2;
    }
    else if (p2->pFile == 0)
    {
        iRes = i1;
    }
    else /*  */
    {
        int res;
        assert(pCsr->pSorter->pUnpacked != 0);  /* allocated in vdbeSorterMerge() */
        vdbeSorterCompare(
            pCsr, 0, p1->aKey, p1->nKey, p2->aKey, p2->nKey, &res
        );
        if (res <= 0)
        {
            iRes = i1;
        }
        else
        {
            iRes = i2;
        }
    }

    pSorter->aTree[iOut] = iRes; /* 记录下比较的结果,也就是迭代器的下标 */
    return SQLITE_OK;
}

/*
** Initialize the temporary index cursor just opened as a sorter cursor.
** 初始化一个临时的sorter游标
*/
int sqlite3VdbeSorterInit(sqlite3 *db, VdbeCursor *pCsr)
{
    int pgsz;                       /* Page size of main database */
    int mxCache;                    /* Cache size */
    VdbeSorter *pSorter;            /* The new sorter */
    char *d;                        /* Dummy */

    assert(pCsr->pKeyInfo && pCsr->pBt == 0);
    pCsr->pSorter = pSorter = sqlite3DbMallocZero(db, sizeof(VdbeSorter));
    if (pSorter == 0)
    {
        return SQLITE_NOMEM;
    }

    pSorter->pUnpacked = sqlite3VdbeAllocUnpackedRecord(pCsr->pKeyInfo, 0, 0, &d);
    if (pSorter->pUnpacked == 0) return SQLITE_NOMEM;
    assert(pSorter->pUnpacked == (UnpackedRecord *)d);

    if (!sqlite3TempInMemory(db))
    {
        pgsz = sqlite3BtreeGetPageSize(db->aDb[0].pBt); /* 页的大小 */
        pSorter->mnPmaSize = SORTER_MIN_WORKING * pgsz;
        mxCache = db->aDb[0].pSchema->cache_size;
        if (mxCache < SORTER_MIN_WORKING) mxCache = SORTER_MIN_WORKING;
        pSorter->mxPmaSize = mxCache * pgsz;
    }

    return SQLITE_OK;
}

/*
** Free the list of sorted records starting at pRecord.
*/
static void vdbeSorterRecordFree(sqlite3 *db, SorterRecord *pRecord)
{
    SorterRecord *p;
    SorterRecord *pNext;
    for (p = pRecord; p; p = pNext)
    {
        pNext = p->pNext;
        sqlite3DbFree(db, p);
    }
}

/*
** Free any cursor components allocated by sqlite3VdbeSorterXXX routines.
*/
void sqlite3VdbeSorterClose(sqlite3 *db, VdbeCursor *pCsr)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    if (pSorter)
    {
        if (pSorter->aIter)
        {
            int i;
            for (i = 0; i < pSorter->nTree; i++)
            {
                vdbeSorterIterZero(db, &pSorter->aIter[i]);
            }
            sqlite3DbFree(db, pSorter->aIter);
        }
        if (pSorter->pTemp1)
        {
            sqlite3OsCloseFree(pSorter->pTemp1);
        }
        vdbeSorterRecordFree(db, pSorter->pRecord);
        sqlite3DbFree(db, pSorter->pUnpacked);
        sqlite3DbFree(db, pSorter);
        pCsr->pSorter = 0;
    }
}

/*
** Allocate space for a file-handle and open a temporary file. If successful,
** set *ppFile to point to the malloc'd file-handle and return SQLITE_OK.
** Otherwise, set *ppFile to 0 and return an SQLite error code.
*/
static int vdbeSorterOpenTempFile(sqlite3 *db, sqlite3_file **ppFile)
{
    int dummy;
    return sqlite3OsOpenMalloc(db->pVfs, 0, ppFile,
                               SQLITE_OPEN_TEMP_JOURNAL |
                               SQLITE_OPEN_READWRITE    | SQLITE_OPEN_CREATE |
                               SQLITE_OPEN_EXCLUSIVE    | SQLITE_OPEN_DELETEONCLOSE, &dummy
                              );
}

/*
** Merge the two sorted lists p1 and p2 into a single list.
** Set *ppOut to the head of the new list.
** 将两个有序链表p1以及p2合并成单个链表,将*ppOut设置为新链表的头部
*/
static void vdbeSorterMerge(
    const VdbeCursor *pCsr,         /* For pKeyInfo */
    SorterRecord *p1,               /* First list to merge */
    SorterRecord *p2,               /* Second list to merge */
    SorterRecord **ppOut            /* OUT: Head of merged list */
)
{
    SorterRecord *pFinal = 0;
    SorterRecord **pp = &pFinal;
    void *pVal2 = p2 ? p2->pVal : 0;
    /* 举一个例子:
    ** p1-\
    **     1 --> 3 --> 5
    ** p2-\
    **     2 --> 4 --> 6
    ** 排序完成之后:
    ** pFinal--\
    **         1 --> 2 --> 3 --> 4 --> 5 --> 6
    **
    */
    while (p1 && p2)
    {
        int res;
        vdbeSorterCompare(pCsr, 0, p1->pVal, p1->nVal, pVal2, p2->nVal, &res);
        if (res <= 0)
        {
            *pp = p1; /* p1指向值比较小 */
            pp = &p1->pNext;
            p1 = p1->pNext; /* p1移动到下一个记录 */
            pVal2 = 0;
        }
        else
        {
            *pp = p2; /* p2指向的值比较小 */
            pp = &p2->pNext;
            p2 = p2->pNext;
            if (p2 == 0) break;
            pVal2 = p2->pVal;
        }
    }
    *pp = p1 ? p1 : p2;
    *ppOut = pFinal;
}

/*
** Sort the linked list of records headed at pCsr->pRecord. Return SQLITE_OK
** if successful, or an SQLite error code (i.e. SQLITE_NOMEM) if an error
** occurs.
** 对链表进行排序操作.链表头部记录在pCsr->pRecord中,如果成功了,返回SQLite_OK
*/
static int vdbeSorterSort(const VdbeCursor *pCsr)
{
    int i;
    SorterRecord **aSlot;
    SorterRecord *p;
    VdbeSorter *pSorter = pCsr->pSorter;
    /* 这里一共分配了64个slot */
    aSlot = (SorterRecord **)sqlite3MallocZero(64 * sizeof(SorterRecord *));
    if (!aSlot)
    {
        return SQLITE_NOMEM;
    }

    p = pSorter->pRecord;
    while (p)
    {
        SorterRecord *pNext = p->pNext;
        p->pNext = 0; /* 这里等同于将p从链表中移除 */
        for (i = 0; aSlot[i]; i++)
        {
            /* 将p与Slot[i]链表合并,p指向排序后的链表首部 */
            vdbeSorterMerge(pCsr, p, aSlot[i], &p); 
            aSlot[i] = 0;
        }
        aSlot[i] = p; /* 记录下有序链表的第一个元素 */
        p = pNext; /* 指向下一条记录 */
    }

    p = 0;
    for (i = 0; i < 64; i++) /* 然后对前后两组链表进行合并 */
    {
        vdbeSorterMerge(pCsr, p, aSlot[i], &p);
    }
    pSorter->pRecord = p; /* 最终得到有序的链表p */

    sqlite3_free(aSlot);
    return SQLITE_OK;
}

/*
** Initialize a file-writer object.
** 构建一个file-writer
*/
static void fileWriterInit(
    sqlite3 *db,                    /* Database (for malloc) */
    sqlite3_file *pFile,            /* File to write to */
    FileWriter *p,                  /* Object to populate */
    i64 iStart                      /* Offset of pFile to begin writing at */
)
{
    int nBuf = sqlite3BtreeGetPageSize(db->aDb[0].pBt);

    memset(p, 0, sizeof(FileWriter));
    p->aBuffer = (u8 *)sqlite3DbMallocRaw(db, nBuf);
    if (!p->aBuffer)
    {
        p->eFWErr = SQLITE_NOMEM;
    }
    else
    {
        p->iBufEnd = p->iBufStart = (iStart % nBuf);
        p->iWriteOff = iStart - p->iBufStart;
        p->nBuffer = nBuf;
        p->pFile = pFile;
    }
}

/*
** Write nData bytes of data to the file-write object. Return SQLITE_OK
** if successful, or an SQLite error code if an error occurs.
*/
static void fileWriterWrite(FileWriter *p, u8 *pData, int nData)
{
    int nRem = nData;
    while (nRem > 0 && p->eFWErr == 0)
    {
        int nCopy = nRem;
        if (nCopy > (p->nBuffer - p->iBufEnd))
        {
            nCopy = p->nBuffer - p->iBufEnd;
        }

        memcpy(&p->aBuffer[p->iBufEnd], &pData[nData - nRem], nCopy);
        p->iBufEnd += nCopy;
        if (p->iBufEnd == p->nBuffer)
        {
            p->eFWErr = sqlite3OsWrite(p->pFile,
                                       &p->aBuffer[p->iBufStart], p->iBufEnd - p->iBufStart,
                                       p->iWriteOff + p->iBufStart
                                      );
            p->iBufStart = p->iBufEnd = 0;
            p->iWriteOff += p->nBuffer;
        }
        assert(p->iBufEnd < p->nBuffer);

        nRem -= nCopy;
    }
}

/*
** Flush any buffered data to disk and clean up the file-writer object.
** The results of using the file-writer after this call are undefined.
** Return SQLITE_OK if flushing the buffered data succeeds or is not
** required. Otherwise, return an SQLite error code.
**
** Before returning, set *piEof to the offset immediately following the
** last byte written to the file.
*/
static int fileWriterFinish(sqlite3 *db, FileWriter *p, i64 *piEof)
{
    int rc;
    if (p->eFWErr == 0 && ALWAYS(p->aBuffer) && p->iBufEnd > p->iBufStart)
    {
        p->eFWErr = sqlite3OsWrite(p->pFile,
                                   &p->aBuffer[p->iBufStart], p->iBufEnd - p->iBufStart,
                                   p->iWriteOff + p->iBufStart
                                  );
    }
    *piEof = (p->iWriteOff + p->iBufEnd);
    sqlite3DbFree(db, p->aBuffer);
    rc = p->eFWErr;
    memset(p, 0, sizeof(FileWriter));
    return rc;
}

/*
** Write value iVal encoded as a varint to the file-write object. Return
** SQLITE_OK if successful, or an SQLite error code if an error occurs.
*/
static void fileWriterWriteVarint(FileWriter *p, u64 iVal)
{
    int nByte;
    u8 aByte[10];
    nByte = sqlite3PutVarint(aByte, iVal);
    fileWriterWrite(p, aByte, nByte);
}

/*
** Write the current contents of the in-memory linked-list to a PMA. Return
** SQLITE_OK if successful, or an SQLite error code otherwise.
** 将内存中链表中的内容写入一个PMA,如果成功的话,返回SQLITE_OK,否则的话返回SQLite错误.
**
** The format of a PMA is:
** PMA的格式如下:
**
**     * A varint. This varint contains the total number of bytes of content
**       in the PMA (not including the varint itself).
**     * 一个变长变量,用于表示PMA内容所占的字节数(不包含变量本身)
**
**     * One or more records packed end-to-end in order of ascending keys.
**       Each record consists of a varint followed by a blob of data (the
**       key). The varint is the number of bytes in the blob of data.
**     * 一个或者多个记录,按照key升序一个接着一个存储,每条记录包含一个变长变量,然后是blob类型的数据
**       变长变量记录了blob类型数据的所占用的字节数
*/
static int vdbeSorterListToPMA(sqlite3 *db, const VdbeCursor *pCsr)
{
    int rc = SQLITE_OK;             /* Return code */
    VdbeSorter *pSorter = pCsr->pSorter;
    FileWriter writer;

    memset(&writer, 0, sizeof(FileWriter));

    if (pSorter->nInMemory == 0)
    {
        assert(pSorter->pRecord == 0);
        return rc;
    }

    rc = vdbeSorterSort(pCsr);

    /* If the first temporary PMA file has not been opened, open it now. */
    if (rc == SQLITE_OK && pSorter->pTemp1 == 0)
    {
        /* 打开文件 */
        rc = vdbeSorterOpenTempFile(db, &pSorter->pTemp1);
        assert(rc != SQLITE_OK || pSorter->pTemp1);
        assert(pSorter->iWriteOff == 0);
        assert(pSorter->nPMA == 0);
    }

    if (rc == SQLITE_OK)
    {
        SorterRecord *p;
        SorterRecord *pNext = 0;

        fileWriterInit(db, pSorter->pTemp1, &writer, pSorter->iWriteOff);
        pSorter->nPMA++;
        fileWriterWriteVarint(&writer, pSorter->nInMemory);
        for (p = pSorter->pRecord; p; p = pNext) /* 将排序后的每一条记录都写入磁盘 */
        {
            pNext = p->pNext;
            fileWriterWriteVarint(&writer, p->nVal);
            fileWriterWrite(&writer, p->pVal, p->nVal);
            sqlite3DbFree(db, p);
        }
        pSorter->pRecord = p;
        rc = fileWriterFinish(db, &writer, &pSorter->iWriteOff);
    }

    return rc;
}

/*
** Add a record to the sorter.
** 添加一条记录到sorter之中
*/
int sqlite3VdbeSorterWrite(
    sqlite3 *db,                    /* Database handle */
    const VdbeCursor *pCsr,               /* Sorter cursor */
    Mem *pVal                       /* Memory cell containing record */
)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    int rc = SQLITE_OK;             /* Return Code */
    SorterRecord *pNew;             /* New list element */

    assert(pSorter);
    pSorter->nInMemory += sqlite3VarintLen(pVal->n) + pVal->n;

    pNew = (SorterRecord *)sqlite3DbMallocRaw(db, pVal->n + sizeof(SorterRecord));
    if (pNew == 0)
    {
        rc = SQLITE_NOMEM;
    }
    else
    {
        pNew->pVal = (void *)&pNew[1];
        memcpy(pNew->pVal, pVal->z, pVal->n);
        pNew->nVal = pVal->n;
        pNew->pNext = pSorter->pRecord;
        pSorter->pRecord = pNew;
    }

    /* See if the contents of the sorter should now be written out. They
    ** are written out when either of the following are true:
    ** 检查是否需要将sourter中的内容写入磁盘,检查内容如下:
    **
    **   * The total memory allocated for the in-memory list is greater
    **     than (page-size * cache-size), or
    **   * 内存中链表分配的总内存数目大于 page-size * cache-size
    **
    **   * The total memory allocated for the in-memory list is greater
    **     than (page-size * 10) and sqlite3HeapNearlyFull() returns true.
    **   * 内存中链表的分配总数大于 page-size * 10并且sqlite3HeapNearlyFull()返回真.
    */
    if (rc == SQLITE_OK && pSorter->mxPmaSize > 0 && (
            (pSorter->nInMemory > pSorter->mxPmaSize)
            || (pSorter->nInMemory > pSorter->mnPmaSize && sqlite3HeapNearlyFull())
        ))
    {
#ifdef SQLITE_DEBUG
        i64 nExpect = pSorter->iWriteOff
                      + sqlite3VarintLen(pSorter->nInMemory)
                      + pSorter->nInMemory;
#endif
        rc = vdbeSorterListToPMA(db, pCsr);
        pSorter->nInMemory = 0;
        assert(rc != SQLITE_OK || (nExpect == pSorter->iWriteOff));
    }

    return rc;
}

/*
** Helper function for sqlite3VdbeSorterRewind().
** sqlite3VdbeSorterRewind()的辅助函数
*/
static int vdbeSorterInitMerge(
    sqlite3 *db,                    /* Database handle */
    const VdbeCursor *pCsr,         /* Cursor handle for this sorter */
    i64 *pnByte                     /* Sum of bytes in all opened PMAs */
)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    int rc = SQLITE_OK;             /* Return code */
    int i;                          /* Used to iterator through aIter[] */
    i64 nByte = 0;                  /* Total bytes in all opened PMAs */

    /* Initialize the iterators.
    ** 初始化迭代器
    */
    for (i = 0; i < SORTER_MAX_MERGE_COUNT; i++)
    {
        VdbeSorterIter *pIter = &pSorter->aIter[i];
        /* 从文件中读取出PMA */
        rc = vdbeSorterIterInit(db, pSorter, pSorter->iReadOff, pIter, &nByte);
        pSorter->iReadOff = pIter->iEof;
        assert(rc != SQLITE_OK || pSorter->iReadOff <= pSorter->iWriteOff);
        if (rc != SQLITE_OK || pSorter->iReadOff >= pSorter->iWriteOff) break;
    }

    /* Initialize the aTree[] array. 
    ** 初始化aTree[]数组 
    */
    for (i = pSorter->nTree - 1; rc == SQLITE_OK && i > 0; i--)
    {
        rc = vdbeSorterDoCompare(pCsr, i);
    }

    *pnByte = nByte;
    return rc;
}

/*
** Once the sorter has been populated, this function is called to prepare
** for iterating through its contents in sorted order.
** 一旦sorter被安装,此函数用于初始化sorter
*/
int sqlite3VdbeSorterRewind(sqlite3 *db, const VdbeCursor *pCsr, int *pbEof)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    int rc;                         /* Return code */
    sqlite3_file *pTemp2 = 0;       /* Second temp file to use */
    i64 iWrite2 = 0;                /* Write offset for pTemp2 */
    int nIter;                      /* Number of iterators used */
    int nByte;                      /* Bytes of space required for aIter/aTree */
    int N = 2;                      /* Power of 2 >= nIter */

    assert(pSorter);

    /* If no data has been written to disk, then do not do so now. Instead,
    ** sort the VdbeSorter.pRecord list. The vdbe layer will read data directly
    ** from the in-memory list.  */
    /* 如果还没有任何数据被写入磁盘,那么不要现在写
    */
    if (pSorter->nPMA == 0)
    {
        *pbEof = !pSorter->pRecord;
        assert(pSorter->aTree == 0);
        return vdbeSorterSort(pCsr);
    }

    /* Write the current in-memory list to a PMA. */
    rc = vdbeSorterListToPMA(db, pCsr);
    if (rc != SQLITE_OK) return rc;

    /* Allocate space for aIter[] and aTree[]. */
    nIter = pSorter->nPMA;
    if (nIter > SORTER_MAX_MERGE_COUNT) nIter = SORTER_MAX_MERGE_COUNT;
    assert(nIter > 0);
    while (N < nIter) N += N;
    nByte = N * (sizeof(int) + sizeof(VdbeSorterIter));
    pSorter->aIter = (VdbeSorterIter *)sqlite3DbMallocZero(db, nByte);
    if (!pSorter->aIter) return SQLITE_NOMEM;
    pSorter->aTree = (int *)&pSorter->aIter[N];
    pSorter->nTree = N;

    do
    {
        int iNew;                     /* Index of new, merged, PMA */

        for (iNew = 0;
             rc == SQLITE_OK && iNew * SORTER_MAX_MERGE_COUNT < pSorter->nPMA;
             iNew++
            )
        {
            int rc2;                    /* Return code from fileWriterFinish() */
            FileWriter writer;          /* Object used to write to disk */
            i64 nWrite;                 /* Number of bytes in new PMA */

            memset(&writer, 0, sizeof(FileWriter));

            /* If there are SORTER_MAX_MERGE_COUNT or less PMAs in file pTemp1,
            ** initialize an iterator for each of them and break out of the loop.
            ** These iterators will be incrementally merged as the VDBE layer calls
            ** sqlite3VdbeSorterNext().
            **
            ** Otherwise, if pTemp1 contains more than SORTER_MAX_MERGE_COUNT PMAs,
            ** initialize interators for SORTER_MAX_MERGE_COUNT of them. These PMAs
            ** are merged into a single PMA that is written to file pTemp2.
            */
            rc = vdbeSorterInitMerge(db, pCsr, &nWrite);
            assert(rc != SQLITE_OK || pSorter->aIter[ pSorter->aTree[1] ].pFile);
            if (rc != SQLITE_OK || pSorter->nPMA <= SORTER_MAX_MERGE_COUNT)
            {
                break;
            }

            /* Open the second temp file, if it is not already open. */
            /* 打开第2个临时文件 */
            if (pTemp2 == 0)
            {
                assert(iWrite2 == 0);
                rc = vdbeSorterOpenTempFile(db, &pTemp2);
            }

            if (rc == SQLITE_OK)
            {
                int bEof = 0;
                fileWriterInit(db, pTemp2, &writer, iWrite2);
                fileWriterWriteVarint(&writer, nWrite);
                while (rc == SQLITE_OK && bEof == 0)
                {
                    VdbeSorterIter *pIter = &pSorter->aIter[ pSorter->aTree[1] ];
                    assert(pIter->pFile);
                    /* 将记录写入文件 */
                    fileWriterWriteVarint(&writer, pIter->nKey);
                    fileWriterWrite(&writer, pIter->aKey, pIter->nKey);
                    rc = sqlite3VdbeSorterNext(db, pCsr, &bEof);
                }
                rc2 = fileWriterFinish(db, &writer, &iWrite2);
                if (rc == SQLITE_OK) rc = rc2;
            }
        }

        if (pSorter->nPMA <= SORTER_MAX_MERGE_COUNT)
        {
            break;
        }
        else
        {
            sqlite3_file *pTmp = pSorter->pTemp1;
            pSorter->nPMA = iNew;
            pSorter->pTemp1 = pTemp2;
            pTemp2 = pTmp;
            pSorter->iWriteOff = iWrite2;
            pSorter->iReadOff = 0;
            iWrite2 = 0;
        }
    }
    while (rc == SQLITE_OK);

    if (pTemp2)
    {
        sqlite3OsCloseFree(pTemp2);
    }
    *pbEof = (pSorter->aIter[pSorter->aTree[1]].pFile == 0);
    return rc;
}

/*
** Advance to the next element in the sorter.
** 指向sorter中的下一个元素
*/
int sqlite3VdbeSorterNext(sqlite3 *db, const VdbeCursor *pCsr, int *pbEof)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    int rc;                         /* Return code */

    if (pSorter->aTree) /* 存在aTree数组,则表示已经排过序了 */
    {
        int iPrev = pSorter->aTree[1];/* Index of iterator to advance */
        int i;                        /* Index of aTree[] to recalculate */

        rc = vdbeSorterIterNext(db, &pSorter->aIter[iPrev]);
        for (i = (pSorter->nTree + iPrev) / 2; rc == SQLITE_OK && i > 0; i = i / 2)
        {
            rc = vdbeSorterDoCompare(pCsr, i);
        }

        *pbEof = (pSorter->aIter[pSorter->aTree[1]].pFile == 0);
    }
    else
    {
        SorterRecord *pFree = pSorter->pRecord;
        pSorter->pRecord = pFree->pNext;
        pFree->pNext = 0;
        vdbeSorterRecordFree(db, pFree);
        *pbEof = !pSorter->pRecord;
        rc = SQLITE_OK;
    }
    return rc;
}

/*
** Return a pointer to a buffer owned by the sorter that contains the
** current key.
** 
*/
static void *vdbeSorterRowkey(
    const VdbeSorter *pSorter,      /* Sorter object */
    int *pnKey                      /* OUT: Size of current key in bytes */
)
{
    void *pKey;
    if (pSorter->aTree)
    {
        VdbeSorterIter *pIter;
        pIter = &pSorter->aIter[ pSorter->aTree[1] ];
        *pnKey = pIter->nKey;
        pKey = pIter->aKey;
    }
    else
    {
        *pnKey = pSorter->pRecord->nVal;
        pKey = pSorter->pRecord->pVal;
    }
    return pKey;
}

/*
** Copy the current sorter key into the memory cell pOut.
*/
int sqlite3VdbeSorterRowkey(const VdbeCursor *pCsr, Mem *pOut)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    void *pKey;
    int nKey;           /* Sorter key to copy into pOut */

    pKey = vdbeSorterRowkey(pSorter, &nKey);
    if (sqlite3VdbeMemGrow(pOut, nKey, 0))
    {
        return SQLITE_NOMEM;
    }
    pOut->n = nKey;
    MemSetTypeFlag(pOut, MEM_Blob);
    memcpy(pOut->z, pKey, nKey);

    return SQLITE_OK;
}

/*
** Compare the key in memory cell pVal with the key that the sorter cursor
** passed as the first argument currently points to. For the purposes of
** the comparison, ignore the rowid field at the end of each record.
**
** If an error occurs, return an SQLite error code (i.e. SQLITE_NOMEM).
** Otherwise, set *pRes to a negative, zero or positive value if the
** key in pVal is smaller than, equal to or larger than the current sorter
** key.
*/
int sqlite3VdbeSorterCompare(
    const VdbeCursor *pCsr,         /* Sorter cursor */
    Mem *pVal,                      /* Value to compare to current sorter key */
    int *pRes                       /* OUT: Result of comparison */
)
{
    VdbeSorter *pSorter = pCsr->pSorter;
    void *pKey;
    int nKey;           /* Sorter key to compare pVal with */

    pKey = vdbeSorterRowkey(pSorter, &nKey);
    vdbeSorterCompare(pCsr, 1, pVal->z, pVal->n, pKey, nKey, pRes);
    return SQLITE_OK;
}

#endif /* #ifndef SQLITE_OMIT_MERGE_SORT */
