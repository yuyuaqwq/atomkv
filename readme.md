## yudb

yudb是一个使用B-Tree作为索引，基于MMap的嵌入式键值数据库，使用C++开发，旨在改进LMDB的写事务的性能问题，并简化使用。

## 特征

- key-value是任意字节数组，按key排序
- 对数时间的查找/插入/删除
- 支持事务，遵循ACID特性
- 基于MVCC的读写事务并发
- 支持Bucket的嵌套
- 使用mmap，但自动扩展大小
- 可配置的Comparator

## 局限性

- key最长存储为一页
- 同一时间仅允许一个写事务
- 支持多进程打开数据库，但使用读写锁
  - 未来可能得到改进

## 陷阱

- 过长的写事务可能会使日志文件无法及时清理
- 更新后迭代器会失效(Put、Delete、Update)
- Checkpoint是同步执行的，会导致周期性的性能下降
  - 未来可能得到改进

## 表现

- todo：