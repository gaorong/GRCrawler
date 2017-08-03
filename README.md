# GRCrawler      
- GRCrawler is an open source web crawler for C++, which is customizable and high-performance.     
- Based on the Reactor pattern(epoll), supports Http 1.1(persistent connections, pipelining, chunked transfer-encoding).     
- Using Bloom Filter, ThreadPool, async DNS parse etc, imporve the performance.   
- Custom the feature by the configuration file for basic and add the DSO(dynamic shared object which is ".so" file) for advanced.



## Requirements   

- libevent    
- Works on Linux

## Installation      

```bash
 $make && make install
```

## Quickstart   

You need to config the crawler by `spider.conf`, and run the `spider`, that's all.   
Moreover, "-d" option assign running the program as a daemon process.
 





