# Delete.c中主要函数的实现及功能

delete.c中的主要函数包括以下几个:

```c
Table *sqlite3SrcListLookup()
int sqlite3IsReadOnly()
void sqlite3MaterializeView()
Expr *sqlite3LimitWhere()
void sqlite3DeleteFrom()
void sqlite3GenerateRowDelete()
void sqlite3GenerateRowIndexDelete()
int sqlite3GenerateIndexKey( )
```

接下来分别介绍这些函数.

## sqlite3SrcListLookup
```c
Table *sqlite3SrcListLookup(Parse *pParse, SrcList *pSrc)
{
  struct SrcList_item *pItem = pSrc->a;
  Table *pTab;
  assert( pItem && pSrc->nSrc==1 );
  pTab = sqlite3LocateTable(pParse, 0, pItem->zName, pItem->zDatabase);
  sqlite3DeleteTable(pParse->db, pItem->pTab);
  pItem->pTab = pTab;
  if( pTab ){
    pTab->nRef++;
  }
  if( sqlite3IndexedByLookup(pParse, pItem) ){
    pTab = 0;
  }
  return pTab;
}
```

函数的作用查找所有名字为pSrc的表,如果没有找到任何表,添加一个错误消息`pParse->zErrMsg`并返回NULL.如果所有的表都找到,返回一个指针,指向最后一个表.

## sqlite3IsReadOnly
```c
int sqlite3IsReadOnly(Parse *pParse, Table *pTab, int viewOk){
if( ( IsVirtual(pTab)
&& sqlite3GetVTable(pParse->db, pTab)->pMod->pModule->xUpdate==0 )
|| ( (pTab->tabFlags & TF_Readonly)!=0
&& (pParse->db->flags & SQLITE_WriteSchema)==0
&& pParse->nested==0 )
){
 sqlite3ErrorMsg(pParse, "table %s may not be modified", pTab->zName);
 return 1;
}
```
函数目的是检查以确保给定的表是可写的.如果它不是可写,生成一条错误消息,并返回1.如果它是可写返回0.

调用了sqlite3ErrorMsg()函数.

## sqlite3MaterializeView
```c
void sqlite3MaterializeView(
  Parse *pParse,       /* Parsing context */
  Table *pView,        /* View definition */
  Expr *pWhere,        /* Optional WHERE clause to be added */
  int iCur             /* Cursor number for ephemerial table */
){
  SelectDest dest;
  Select *pDup;
  sqlite3 *db = pParse->db;
  pDup = sqlite3SelectDup(db, pView->pSelect, 0);
  if( pWhere ){
    SrcList *pFrom;
    pWhere = sqlite3ExprDup(db, pWhere, 0);
    pFrom = sqlite3SrcListAppend(db, 0, 0, 0);
    if( pFrom ){
      assert( pFrom->nSrc==1 );
      pFrom->a[0].zAlias = sqlite3DbStrDup(db, pView->zName);
      pFrom->a[0].pSelect = pDup;
      assert( pFrom->a[0].pOn==0 );
      assert( pFrom->a[0].pUsing==0 );
    }else{
      sqlite3SelectDelete(db, pDup);
    }
    pDup = sqlite3SelectNew(pParse, 0, pFrom, pWhere, 0, 0, 0, 0, 0, 0);
  }
  sqlite3SelectDestInit(&dest, SRT_EphemTab, iCur);
  sqlite3Select(pParse, pDup, &dest);
  sqlite3SelectDelete(db, pDup);
}
```
函数的结果是把视图存放在一个临时的表空间中,之后用Expr *sqlite3LimitWhere函数产生一个表达树来实现DELETE and UPDATE语句中的WHERE, ORDER BY, LIMIT/OFFSET 部分

调用函数 sqlite3SelectDup()、sqlite3ExprDup()、sqlite3SrcListAppend()、sqlite3DbStrDup()、sqlite3SelectDelete()、sqlite3SelectNew()、sqlite3SelectDestInit()、sqlite3Select()、sqlite3SelectDelete()
## sqlite3LimitWhere
```c
 Expr *sqlite3LimitWhere(
  Parse *pParse,               /* The parser context */
  SrcList *pSrc,               /* the FROM clause -- which tables to scan */
  Expr *pWhere,                /* The WHERE clause.  May be null */
  ExprList *pOrderBy,          /* The ORDER BY clause.  May be null */
  Expr *pLimit,                /* The LIMIT clause.  May be null */
  Expr *pOffset,               /* The OFFSET clause.  May be null */
  char *zStmtType              /* Either DELETE or UPDATE.  For error messages. */
)
```

此函数由如下几部分组成:

1)order by没有limit就输出错误信息.
2)果执行LIMIT/OFFSET语句.我们只需要生成一个选择表达式.
3)如果LIMIT语句为空,OFFSET语句也为空.
4)生成一个选择表达式树来执行DELETE或UPDATE中的LIMIT/OFFSET语句.
5)当需要DELETE/UPDATE树和SELECT子树时,复制FROM子句..
6)生成SELECT表达式树.
7)为DELETE/UPDATE生成新的WHERE ROWID IN子句.
8)出现错误,清理分配的东西.

## sqlite3DeleteFrom
程序段是为DELETE FROM语句生成代码
```c
void sqlite3DeleteFrom(
Parse *pParse,         /* 解析指针 */
SrcList *pTabList,     /* 表中指向要删去内容的指针 */
Expr *pWhere           /* WHERE 语句指针.  可以为空 */
)
```
此函数有如下几部分组成:
1)找到我们想要删除的表.该表必须被放在一个SrcList结构,因为一些为多个表服务的子程序将被调用并且用Srclist参数来代替Table参数.
2)弄清楚是否有触发器,是否删除的这个表是一个视图.
3)如果pTab所指的是一个视图,确保它已经初始化.
4)为表分配游标号和所有的索引.
5)开始解析上下文.
6)开始生成代码.
7)如果我们尝试去删除一个视图,该视图必须在一个临时表中.
8)在WHERE子句中解决列名.
9)如果要统计行数,初始化要删除行的编号的计数器.
10)特殊情况:DELETE语句中没有WHERE子句将删除所有的行,这样很容易删除整个表,在之前的3.6.5版本,这个优化导致行被改变的计数器(由函数sqlite3_count_changes返回的值).
11)通常情况下有一个WHERE子句使得我们通过扫描表并挑选其中的记录去删除.
12)收集要删除行的rowid.
13)在数据库扫描的过程中删除主键写入到列表中的每一个项目,在扫描完成后删除这些项目,因为删除的项目可以更改扫描的顺序.
14)除非这是一个视图,打开要删除表的游标和它的所有索引.如果是一个视图,唯一的影响就是要解除INSTEAD OF触发器.
15)删除行.
16)结束DELETE循环.
17)关闭表上打开的游标和它所有的索引.
18)通过存储在插入到自动增量的表记录到的最大的rowid计数器值的内容更新sqlite_sequence表.
19)返回被删除行的编号,如果这个过程正在生成代码是因为调用了函数sqlite3NestedParse(),没有调用回滚函数.

## sqlite3GenerateRowDelete
```c
void sqlite3GenerateRowDelete(
  Parse *pParse,     /* Parsing context */
  Table *pTab,       /* Table containing the row to be deleted */
  int iCur,          /* Cursor number for the table */
  int iRowid,        /* Memory cell that contains the rowid to delete */
  int count,         /* If non-zero, increment the row change counter */
  Trigger *pTrigger, /* List of triggers to (potentially) fire */
  int onconf         /* Default ON CONFLICT policy for triggers */
)
```
函数有以下几部分实现:

  1)虚拟数据库引擎保证在这这个阶段已分配.
  2)寻找游标iCur 来删除行,如果该行不存在(如果触发器程序已经删除它,这种情况会发生),不要尝试去删除它或解除任何的DELETE触发器
  3)如果任何触发器被解除,分配一些寄存器来使用旧的触发器.
  4)可以在这使用临时的寄存器,也可以尝试避免复制rowid 寄存器的内容.
  5)填充OLD 伪表寄存器数组,这些值将被存在的BEFORE 和AFTER寄存器使用.
  6)调用BEFORE DELETE 触发器程序.
  7)在此寻找要删除行的游标,这可能是BEFORE 触发器代码已经移除了要删除的行,不要尝试去第二次删除该行,不要解除AFTER 触发器.
  8)执行外键约束过程,该调用将检查与本表有关的任何外键约束,该调用不会影响删除行.
  9)删除索引和表条目,如果pTab所指的表是一个视图(在这个情况下对DELETE 语句唯一的影响是解除INSTEAD OF 触发器) 跳过这一步.
  10)通过引用被删除行的外键执行任何处理行 (可能在其他表中)  的CASCADE,SET NULL 或SET DEFAULT 操作
  11)调用AFTER DELETE触发器程序.
  12)如果行在调用BEFORE 触发器之前已经删除跳转到这里.或者一个触发器程序抛出了一个RAISE(IGNORE) 异常.

## sqlite3GenerateRowIndexDelete
```c
void sqlite3GenerateRowIndexDelete(
  Parse *pParse,     /* Parsing and code generating context */
  Table *pTab,       /* Table containing the row to be deleted */
  int iCur,          /* Cursor number for the table */
  int *aRegIdx       /* Only delete if aRegIdx!=0 && aRegIdx[i]>0 */
)
```
函数功能:此过程生成VDBE代码导致与单个表的单个行相关联的所有的索引条目被删除.当这个例程被调用的时候VDBE 必须处于特定的状态.
1.读/写指针指向pTab,这个表包含要删除的行,该表必须通过游标号码iCur 打开.
2.pTab所有索引的读写游标必须用游标号码iCur+i (i 表示第几个索引)打开.
3.iCur 游标必须指向要删除的行.

# sqlite3GenerateIndexKey
```c
 int sqlite3GenerateIndexKey(
  Parse *pParse,     /* Parsing context */
  Index *pIdx,       /* The index for which to generate a key */
  int iCur,          /* Cursor number for the pIdx->pTable table */
  int regOut,        /* Write the new index key to this register */
  int doMakeRec      /* Run the OP_MakeRecord instruction if true */
)
```
函数功能:
生成代码将会产生索引值并把它放在寄存器regOut中,pIdex所指的索引值是通过pTab 打开的表iCur游标所指的索引并且指向需要需索引的条目.返回寄存器块(掌握着索引关键字的元素) 中第一个寄存器的编号,当这个例程返回的时候寄存器块已经被释放了.

