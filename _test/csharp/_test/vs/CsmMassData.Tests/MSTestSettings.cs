// 由于 CsmMassData API 使用进程内共享的环形缓冲区，
// 各测试之间会通过 ConfigMassDataParameterCacheSize 互相影响，
// 因此整个程序集禁止并行执行。
[assembly: DoNotParallelize]
