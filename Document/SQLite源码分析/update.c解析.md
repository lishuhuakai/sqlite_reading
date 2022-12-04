# Update.c中主要函数的实现及功能
Update.c中的主要函数包括以下几个:
```c
void sqlite3ColumnDefault()
void sqlite3Update()
static void updateVirtualTable()
```
## sqlite3ColumnDefault
```c
void sqlite3ColumnDefault(Vdbe *v, Table *pTab, int i, int iReg)
{
  if (!pTab->pSelect)
  {
    sqlite3_value *pValue;
    u8 enc = ENC(sqlite3VdbeDb(v));
    Column *pCol = &pTab->aCol[i];
    VdbeComment((v, "%s.%s", pTab->zName, pCol->zName));
    sqlite3ValueFromExpr(sqlite3VdbeDb(v), pCol->pDflt, enc,
                         pCol->affinity, &pValue);
    if (pValue)
    {
      sqlite3VdbeChangeP4(v, -1, (const char *)pValue, P4_MEM);
    }
    if (iReg>=0 && pTab->aCol[i].affinity==SQLITE_AFF_REAL)
    {
      sqlite3VdbeAddOp1(v, OP_RealAffinity, iReg);
    }
  }
}
```

函数介绍:
最近编码指令是一个OP_Column来检索pTab 所指表中第i 列,这个过程将OP_Column的P4  参数(如果有的话) 设定为默认值,在列定义的DEFAULT 子句中指定列的默认值.
当表建立起来,通过用户提供,或通过ALTER TABLE 命令后来添加到表定义中,如果是后者,那么磁盘上btree表的行记录可能不包含列值和默认值,从 OP_Column 指令的P4 参数所取的值,则返回代替,如果是前者,那么所有行记录对于列有一个值,并且P4的值不是必须的.ALTER TABLE命令创建的列定义可能只有文字指定默认值:一个数字,null或一个字符串.
如果提供了一个更复杂的默认表达式值,当ALTER TABLE语句执行,一个文字值写入sqlite_master table的时候评估.
因此,如果列的默认值是一个文本数字,字符串或null,P4参数是必要的.sqlite3ValueFromExpr()函数可以将这些类型的表达式转换为sqlite3_value对象.如果参数iReg非负,在寄存器iReg上编码一个OP RealAffinity指令.这是当一个相等的整数值存储在一个8字节浮点值的地方,以节省空间.

## updateVirtualTable

```c
 static void updateVirtualTable(
  Parse *pParse,       /* The parsing context */
  SrcList *pSrc,       /* The virtual table to be modified */
  Table *pTab,         /* The virtual table */
  ExprList *pChanges,  /* The columns to change in the UPDATE statement */
  Expr *pRowid,        /* Expression used to recompute the rowid */
  int *aXRef,          /* Mapping from columns of pTab to entries in pChanges */
  Expr *pWhere,        /* WHERE clause of the UPDATE statement */
  int onError          /* ON CONFLICT strategy */
)
```

函数功能介绍:
生成虚拟表更新代码, 创建一个临时表,该表包含要修改的每一行
A)该行最初的ROWID.
B)正后的行ROWID.
C)行中每一列的内容.
然后我们遍历这个临时表,这个临时表中的每一行调用VUpdate.((注1)其实,如果我们事先知道(A) 始终与(B) 是一样的,我们仅存储(A),在调用VUpdate之前,当把它从临时表中弄出来的时候复制A.函数由以下几部分组成:<pre>
1)构建SELECT语句,会发现所有更新的行的新值.
2)创建临时表到更新结果被保存.
3)填补临时表
4)生成代码来扫描临时表和调用VUpdate.
5)清理


## sqlite3Update
```c
void sqlite3Update(
  Parse *pParse,         /* The parser context */
  SrcList *pTabList,     /* The table in which we should change things */
  ExprList *pChanges,    /* Things to be changed */
  Expr *pWhere,          /* The WHERE clause.  May be null */
  int onError            /* How to handle constraint errors */
)
```

函数介绍:这是一个更新函数.

此函数有以下几部分组成:
1)寄存器分配.
2)到我们想要更新的表.
3)找出如果有触发器并且更新的这个表是一个视图.
4)给主要的数据库表和所有的目录分配指针,索引的指针可能不被使用,但是如果使用索引的指针,他们应该发生在数据库指针的右面,所以继续分配足够的空间以备不时之需.
5)初始化上下文的名称.
6)解决更新语句所有表达式的列名,并且在pChange数组中找到要更新的每一个列的列索引,于每一个要更新的列,确保我们有权限改变列.
7)为数组aRegIdx[]分配内存,对于被更新表的每个索引,在这个数组中都有一个入口,填写的值的寄存器数量指数使用和未使用的指数为零.
8)开始生成代码.
9)虚拟表必须单独处理.
10)分配所需的寄存器.
11)开始解析上下文.
12)如果我们尝试更新一个视图,必须要注意到视图是一个临时表.
13)解决where子句所有表达式的列名.
14)开始扫描数据库.
15)着要被更新的每一项的rowid.
16)结束数据库的扫描循环.
17)初始化更新的行数.
18)打开每个需要更新的索引,需要注意的是如果任何索引都能够潜在的调用REPLACE冲突,那么我们需要打开所有的指标,因为我们可能需要删除一些记录.
19)置顶更新循环.
20)游标iCur 指向要被更新的记录,如果此记录出于某种原因不存在(触发器程序引起的删除,比如跳转到RowSet循环的下一次迭代).
21)果记录号码改变,设置寄存器regNewRowid 包含新值.如果记录号没有被修改,那么regNewRowid 与regOldRowid 是同一寄存器,并且已经被填充
22)如果该表上存在触发器,用old.* column 的数据来填充寄存器数组.
23)这个分支加载的列值不会在寄存器中改变,这种情况在以下条件下发生:没有BEFORE 触发器,或者有一个/多个BEFORE 触发器通过触发器程序使用这个值.
24)除BEFORE触发器,这发生在约束验证之前,有人认为这是错误的.
25)触发器可能已经删除了要更新的行,在这种情况下跳转到下一行.不更新或者AFTER 触发器是必须的.当被更新的行被删除或者被BEFORE 触发器重新命名,这种行为在文档中未定义.
26)如果不删除它,行触发器仍可能修改被修改行的某些列,如果出现这种情况,加载所有列值不会被UPDATE 语句修改进他们的寄存器27)做约束检查.
28)做外键约束检查.
29)删除与当前记录相关联的索引条目..
30)如果更新记录号,删除旧的记录
31)插入新的索引条目和新记录.
32)通过引用被删除行的外键执行任何处理行 (可能在其他表中)  的CASCADE,SET NULL 或SET DEFAULT 操作.
33)递增行计数器.
34)对下一个要更行的记录重复上述操作,直到被WHERE 子句中选择的所有的记录被更新.
35)关闭所有表
36)通过存储在插入到自动增量的表记录到的最大的rowid计数器值的内容更新sqlite_sequence表..
37)返回被删除行的编号,如果这个过程正在生成代码是因为调用了函数sqlite3NestedParse(),没有调用回滚函数
38)确保它是一个视图以及它上边没有定义的其他宏定义,要不然它们这个文件中其他功能的汇编(或者在另一文件中,如果这个文件成为合并的一部分)

