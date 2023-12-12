# threadPool
基于可变参模板实现的线程池<br>
平台工具：vs2022开发，ubuntu1-22.04  g++编译so库<br>
1、基于可便参模板编程和引用折叠原理，实现线程池submitTask接口，支持任意任务函数和任意参数的传递。<br>
2、使用map和queue容器管理线程对象和任务。<br>
3、基于条件变量condition_variable和互斥锁mutex实现任务提交和任务执行线程间通信机制。<br>
4、支持fixed和cached模式的线程池。<br>
