该部分分析了Insert.c文件中各函数定义及实现.

## insert的主要关系

主要的入口是sqlite3Insert()函数,当一条insert语句经过词法分析器,语法分析器,到代码生成器,主要调用insert.c 的sqlite3Insert()函数,完成代码的生成.

在代码生成器中sqlite3Insert()函数调用生命的函数,去查询数据库字典表中是否存在要查询的表,及  表中索引(还要排除虚拟表),对语法分析得到的数据表,数据列,索引列进行比对,查看是否符合条件的约束,insert的触发器,唯一性等的约束,针对integer主键的问题是可以不给定有数据库中给定相关数据(表中最大值),还有就是用户可以给定表中没有的,在insert语句行含有select子句的还要考虑查询数据的关联性,是否需要额外的临时表,或者是有怎样的查询效率等的优化.在进行数据插入时,还有就是各种可能会使数据操作报出异常,而需要终止或回滚的返回.来保持数据的一致性.最终返回的vdbe 的指令集,看懂这些指令集需要去找最新的指令集参照(在此我没有找到).

## sqlite3OpenTable()

输入参数: Parse *p, int iCur, int iDb, Table *pTab, int opcode
返回值:void
函数的作用:函数实现打开一张用于读取数据操作的数据库表.
解释:
输入参数依次:指向解析器的指针p,表中游标的数量,在数据库连接实例索引,指向数据库表的指针pTab,读表或是写表操作;在此打开数据表是为了插入数据,固此函数中有加锁函数的调用对数据表进行  加锁.并向指令集中添加相关操作的Vdbe指令

## sqlite3IndexAffinityStr()

输入参数:Vdbe *v, Index *pIdx
返回值:const char *
函数作用:返回一个与索引pIdx相关的列相关字符串的指针的值而不用修改它,根据列关联,表中每一个列都有一个字符代表列关联的数据类型.
解释:
输入参数依次是指向虚拟机的指针v,指向索引的指针pIdx;返回值是常量指针(不可变的);对于一个特定的索引来说,一个列关联字符串在第一次才需要分配和赋值的.然后对于后续的使用来说,它将会作为一个索引的成员存储;当索引结构本身被清除了的时候,最终这个列关联的字符串通过sqliteDeleteIndex()函数删除.

## sqlite3TableAffinityStr()

输入参数:Vdbe *v, Table *pTab
返回值:void
函数作用:该函数主要是用于把表和与列相关的字符关联在一起.
解释:
输入参数依次是指向虚拟机的指针v,指向数据表的指针pTab;对于一个特定的索引来说,一个列关联字符串在第一次才需要分配和赋值的.然后对于后续的使用,它将会作为一个索引结构成员存储.当索引结构本身被清除了的时候,最终这个列关联的字符串通过sqliteDeleteIndex()函数删除.如果表pTab在  数据库索引数组中,或是它的索引在任何时候被打开在VDBE程序中的开始位置到结束位置的话,返回非零值.这是用于检查"INSERT INTO <iDb, pTab> SELECT ..."规则的语句是否对于查询结构没有临时表的情况下可以运行.

## readsTable()

输入参数:Parse *p, int iStartAddr, int iDb, Table *pTab
返回值:static int
函数作用:申明一个静态函数仅限于表文件调用.
解释:
输入参数依次是解析器指针p,起始地址,数据库连接实例,表指针pTab;主要是在以后的数据库字典中查找进行插入的表是否存在.

## sqlite3AutoincrementBegin()

输入参数:Parse *pParse
返回值:void
函数作用:使用自动增量跟踪初始化的所有的注册
解释:自动生成的数据,生成相关的具体指令集的过程.

## autoIncBegin()

输入参数:Parse *pParse, int iDb, Table *pTab
返回值:static int
函数作用:在数据库中,找到或是创建一个与表pTab关联的AutoincInfo的结构.尽管相同的表用于插入到触发器中自动增加多次,但是每一张表至多有一个AutoincInfo结构.如果是第一次使用表pTab,将会新建一个AutoincInfo结构.在原始的AutoincInfo结构将在以后继续使用.分配三个内存位置:

(1)第一个注册保存表的名字.

(2)第一个注册保存表最大的行号.

(3)第三个注册保存表的sqlite序列的行号.所有插入程序必须知道第二个寄存器将作为返回.返回int数据库类型的静态函数.传入参数依次为解析器环境,数据库保存表的索引,我们需要的表.
解释:主要是那些个自增长策略生成的数据,使用的AutoincInfo结构,一般是不断地增长,如果表中的  值被删除,则相关的数据不会被填补.值是一直在增长.



## autoIncStep()

输入参数:Parse *pParse, int memId, int regRowid
返回值:static void
函数作用:为一个自增计算器跟新最大的行Id,这个例程当前这个堆栈的顶端即将插入一个新的行id时运行.如果这个新的行Id比在MemId内存单元中的最大行id大时,那么这个内存单元将被更新.这个堆栈  是不可改变的.
解释:memID是内存中的ID的值;调用Vdbeaux.c文件中的sqlite3VdbeAddOp2函数.



## sqlite3AutoincrementEnd()

输入参数:Parse *pParse
返回值:void
函数作用:这个例程产生的代码必须把自增的最大行id的值写回到sqlite_sequence寄存器中.仅在这个  "exit"代码之前,每一条可能把一个INSERT写入到一个自增表(直接或间接地通过寄存器)中需要调用这个例程.
解释:主要是结束增长,获取增长的数据,作为要使用的数据插入到数据表当中.



## xferOptimization()

输入参数:Parse *pParse, Table *pDest, Select *pSelect, int onError, int iDbDest
返回值:static int
函数作用:函数的前置声明,优化查询(insert into tab select xxx from tab2形式的语句)
解释:传入参数依次为:解析器环境,要插入的表,SELECT语句数据源,处理约束的错误,Pdest数据库,主要是针对数据表的数据进行处理.

## sqlite3Insert()

输入参数:Parse *pParse, SrcList *pTabList, ExprList *pList, Select *pSelect, IdList *pColumn, int onError
返回值:void
函数作用:插入函数所带参数为:解析器环境,我们要插入的表的表名,要插入的列这个函数例程完  成的工作.
解释:具体的工作是:这个例程调用的时候,SQL是遵循以下形式处理的:
(1)insert into TABLE (IDLIST) values(EXPRLIST)/插入值
(2)insert into TABLE (IDLIST) select/复制表,此时的目标表示存在的表名后的IDLIST通常是可选的.如果省略不写,那么表的所有列的列表替代.IDLIST出现在pColumn的参数中.如果IDLIST省略不写,那么pColumn是空值.pList参数处理(1)式中的EXPRLIST,pSelect是可选的.

对于(2)式,pList是空的,pSelect是一个用于  插入产生数据的查询语句的指针.
代码的生成遵循以下四个当中的一个模板.对于一个简单的查询语句来说,数据来自一个子句的值,代码的执行一次通过.伪代码如下(我们称之为1号模板):

open write cursor to <table> and its indices/打开游标写到表中和索引中puts VALUES clause expressions onto the stack/把子句值表达式压入栈中write the resulting record into <table> cleanup/把结果记录到表中 清除剩下的3个模板假设语句是下面这种形式的:

```c
//	INSERT INTO <table> SELECT ...
```
如果查询子句内容是`SELECT * FROM <table2>` 这种约束形式,也就是说,如果从一个表中获取所有列的查询,就是没有"where"、"limit"、"group by"和"group by 子句".如果表1和表2是有相同模式(包括所有相同的索引)的不同的表,那么从表2中复制原始的记录到表1中涉及到一个特殊的优化.为了实现这//个模板,可以参看这个xferOptimization()函数.

这是第二个模板:

```c
//	open a write cursor to <table>
//	open read cursor on <table2>
//	transfer all records in <table2> over to <table>
//	close cursors
//	foreach index on <table>
//	open a write cursor on the <table> index
//	open a read cursor on the corresponding <table2> index
//	transfer all records from the read to the write cursors
//	close cursors
//	end foreach
```
第三个模板是当第2个模板不能应用和查询子句在任何时候不能读表时.以以下的模板生成代码:
```c
//     EOF <- 0
//       X <- A
//        goto B
//     A: setup for the SELECT
//        loop over the rows in the SELECT
//          load values into registers R..R+n
//         yield X
//        end loop
//        cleanup after the SELECT
//          EOF <- 1
//         yield X
//         goto A
//       B: open write cursor to <table> and its indices
//      C: yield X
//         if EOF goto D
//        insert the select result into <table> from R..R+n
//         goto C
//      D: cleanup
```
第四个模板,如果插入语句从一个查询语句获取值,但是数据要查入到表中,同时表也是查询语句读  取的一部分.在第三种形式中,我们不得不使用中间表存储这个查询的结果.模板如下:
```c
//  EOF <- 0
//          X <- A
//         goto B
//       A: setup for the SELECT
//          loop over the tables in the SELECT
//           load value into register R..R+n
//            yield X
//          end loop
//         cleanup after the SELECT
//          EOF <- 1
//          yield X
//         halt-error
//       B: open temp table
//       L: yield X
//          if EOF goto M
//          insert row from R..R+n into temp table
//          goto L
//      M: open write cursor to <table> and its indices
//          rewind temp table
//       C: loop over rows of intermediate table
//            transfer values form intermediate table into <table>
//         end loop
//      D: cleanup表值,用作数据源的查询语句,与IDLIST相关的列名,处理连续错误.
```
## sqlite3GenerateConstraintChecks()
输入参数:  
```c
			Parse *pParse,      //指向解析器的指针
            Table *pTab,        //要进行插入的表
 			 int baseCur,        //读/写游标的索引在pTab
 			 int regRowid,       //输入寄存器的下标的范围
 			 int *aRegIdx,       //寄存器使用的下标,0是未被使用的指标
			int rowidChng,     //true如果rowid和存在记录冲突时
  			int isUpdate,       //true是更新操作,false是插入操作
  			int overrideError,  //重写onError到这,如果不是OE_Default
  			int ignoreDest,     //跳到这个标签在一个OE_Ignore解决
  			int *pbMayReplace   //输出,如果限制可能引起一个代替动作则设置真
```
返回值:void
函数作用:插入或更新的条件约束检查.
解释:此代码做约束检查当一个插入或更新操作
输入的范围的连贯性表现为以下:
   1.   行ID更新在行更新之后
   2.   数据的第一列在整个记录更新之后更新
      i.   数据来自中间的列的...
      N.  最后一列的数据在整个记录更新之后更新

    那个regRowid参数是寄存器包含的索引,如果参数isUpdate是TRUE并且参数rowidChng不为零则rowidChng包含一个寄存器地址包含rowid 在更新之前发生在isUpdate参数是true表示更新操作,false表示插入操作, 如果isUpdate是false,表明一个insert语句,然而非零的rowidChng参数表明rowid被显示的为insert语句的一部分,如果rowidChng是false意味着rowid是自动计算的在一个插入或是rowid不需要修改的更新操作.这些代码产生由程序,存储新的索引记录到寄存器中通过aRegIdx[]数组来识别.没有记录索引被创建在aRegIdx[i]==0时.指示的顺序在aRegIdx[]与链表(依附于表)的顺序一致,这个程序也会产生代码去检查约束.非空,检查,唯一约束都会被检查.如果约束失效,然后适当的动作被执行.有五种可能的动作:
    ROLLBACK(回滚), ABORT(终止), FAIL(失败), REPLACE(代替), and IGNORE(忽略)

| 约束类型	| 动作|	说明 |
| -- | -- | -- |
| 任何 	 | 回滚	 | 当前的事务被回滚并且sqlite3_exec()返回立即QLITE_CONSTRAINT的代码 |
| 任何 | 终止 | 	 撤销更改从当前的命令中,只(对于已完成的不进行回滚)然而 由于sqlite3_exec()会立即返回SQLITE_CONSTRAINT代码 |
| 任何	 | 失败 | 	Sqlite3_exec()立即返回一个SQLITE_CONSTRAINT代码,事务不会回滚,并且任何  之前的改变都会保留  |
| 任何 | 	忽略 | 记录的数量和数据出现在堆中,并且立即跳到标签 忽略其他 |
| 非空|	代替   |	空值被默认值代替在那列值中,如果默认值为空这动作与终止相同 |
| 唯一 | 	代替 | 	其他行的冲突是被查入行被删掉 |
| 检查|	代替|	非法的,结果集是一个异常|
|哪一个动作被执行取决于被重写的错误的参数.或者如果overrideError == OE_Default,则pParse->onError 参数被使用或者如果pParse->onError==OE_Default则onError的值将被约束使用调用程序必须打开  读/写游标,pTab随着游标的数目计入到baseCur中.|||
|所有的pTab指标必须打开读/写游标随着游标数目baseCur+i对第i个游标,此外,如果不可能有一个代替  动作,则游标不需要打开,在aRegIdx[i]==0.|||

## sqlite3CompleteInsertion()

输入参数: 
```c
			 Parse *pParse,      //指向解析器的指针
 			 Table *pTab,       //我们要插入的表
  			 int baseCur,        //读/写指针指向pTab指数
  			 int regRowid,       //内容的范围
 			 int *aRegIdx,       //寄存器使用的每一个索引,0表示未被使用索引
 			 int isUpdate,       //TRUE是更新,FALSE是插入
 			 int appendBias,     //如果这可能是一个附加,则值为TRUE
 			 int useSeekResult //在OP_[Idx]插入,TRUE设置USESEEKRESULT标记
```
返回值:void
函数作用:程序产生代码来完成插入或者更新操作,之前的函数是生成指令集,此函数是可以正确插入之后要做的.开始通过之前称为sqlite3GenerateConstraintChecks
寄存器连续的范围开始在regRowid	包含rowid和要被插入的数据程序的参数应该和sqlite3GenerateConstraintChecks的前六个参数一样.
解释:此函数在sqlite3Insert被调用,主要是用来断定插入操作的完成.

## sqlite3OpenTableAndIndices()

输入参数: Parse *pParse, Table *pTab, int baseCur,int op
返回值:int
函数作用:此代码将打开一个表的游标并且表的所有索引baseCur参数是表所使用的游标的数量在随后的  游标索引被打开.
解释:主要是打开要操作的数据表及表的所有索引,为后面插入数据做准备.



## xferCompatibleCollation()

输入参数:const char *z1, const char *z2
返回值:static int
函数作用:检查校对名字看他们是否可兼容的,就是做比较调用sqlite3StrICmp().

## xferCompatibleIndex()
输入参数:Index *pDest, Index *pSrc
返回值:static int
函数作用:查看索引pSrc是否是可兼容的作为数据源为了索引pDest在一个插入转移最优中.
可兼容的索引规则:
   *   该索引是在相同的列集合中
   *   DESC 和 ASC 相同标记在所有列上
   *   相同的onError加工过程
   *   相同的校准顺序在每一列上
解释:主要做索引的比较,对比是否是相照应的.