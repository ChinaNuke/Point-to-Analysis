# 国科大编译作业三：Point-to 分析

本次作业是实现 Point-to 分析，实际上要做的事情跟作业二差不多，只不过这次要用看起来更专业的方法。前面让你快跑你不听，现在反悔已经来不及了，这个作业估计要折磨你两到三周的时间，天天写天天写，你会感受到你每天的日常被编译作业充斥是什么样的一种感觉，同时你会不断地重写重写再重写，所以说最开始多花一些时间认真考虑和设计一下数据结构和整体的框架非常非常重要！

本项目的实现总体上还算比较优雅，除了那个将近 200 行的 `handleCallInst` 函数（实际上这个函数有很大的优化空间，很多代码逻辑是可以合并到一起的，不要慌）。通过了除 34 以外的所有测试用例，34 需要框架进行上下文不敏感的分析，但要是做上下文不敏感的分析似乎前面有个测试用例过不了，你可以试试，反正我觉得不值得。

在你开始动手之前我根据经验给你几点建议：

1. 认真考虑清楚 `LoadInst` 和 `StoreInst` 两类指令怎么去处理，一开始可以不全面，但一定要正确，否则你会经历一次重写；
2. 数据结构很重要，用什么样的数据结构存储结果和中间数据，会直接影响你整个项目的风格，一开始考虑得不好很有可能会经历一次重写；
3. 中后期的时候最棘手的是函数调用时如何解决参数的传递，以及函数返回一个指针时如何获取，以及有些测试用例会修改传入的参数的指向，这也是我的 `handleCallInst` 函数写了那么多的原因，这点考虑不清楚可能会经历一次局部重写；
4. 不必花费太多时间去网上找指针分析的相关资料来看，你最后会发现还是得照着中间代码一点一点的去对，前面看的基本用不上。

既然已经没机会退课了，那你就写吧。如果这个仓库有帮助到你，欢迎点亮上面的 Star ，不管你喜不喜欢这个课。祝你在这个大作业中有所收货！

## 下载本仓库

```shell
$ git clone https://github.com/ChinaNuke/Point-to-Analysis.git
```

## Docker 环境

课程提供了一个编译安装好了 llvm 的 docker 环境，你可以直接拿来使用，也可以自己创建一个新的 docker 容器或者虚拟机，自行通过包管理工具或者通过源码编译安装相同版本的 clang 。第二个命令中 `-v` 参数指定一个从本机到 docker 容器的目录映射，这样你就可以直接在本机使用 VSCode 等工具编写代码，在 docker 容器中编译运行，而不用把文件拷来拷去。

```shell
$ docker pull lczxxx123/llvm_10_hw:0.2
$ docker run -td --name llvm_experiment_3 -v "$PWD/Point-to-Analysis":/root/Point-to-Analysis lczxxx123/llvm_10_hw:0.2
$ docker exec -it llvm_experiment_3 bash
```

重新开机后，docker 容器可能没有在运行，你不必再执行一遍 `docker run` 命令，只需要执行 `docker start llvm_experiment_2` 就可以了。如果你还是不太熟悉 docker 命令的话，[这里](https://dockerlabs.collabnix.com/docker/cheatsheet/)有一份 Docker Cheat Sheet 可以查阅。

## 编译中间代码

使用课程提供的脚本将所有测试用例源代码编译成中间代码，生成的文件在 `bc` 目录中。

```shell
$ ./compile.sh
```

或者你也可以使用下面的命令手动把每个测试用例进行编译。

```shell
$ clang -emit-llvm -c -O0 -g3 test/test00.c -o bc/test00.bc
```

## 编译程序

从这一小节开始的所有命令都是在 docker 容器中执行的。创建一个名为 `build` 的目录并进入，使用 `-DLLVM_DIR` 指定 LLVM 的安装路径，`-DCMAKE_BUILD_TYPE=Debug` 参数指明编译时包含符号信息以方便调试。以后的每次编译只需要执行 `make` 就可以了，除非你修改了 CMakeLists.txt 。

```shell
$ mkdir build && cd $_
$ cmake -DLLVM_DIR=/usr/local/llvm10ra/ -DCMAKE_BUILD_TYPE=Debug ..
$ make
```

## 运行

```shell
$ ./llvmassignment3 ../bc/test00.bc
```

## 参考

- https://www.cs.utexas.edu/~pingali/CS380C/2019/lectures/pointsTo.pdf

- https://llvm.org/docs/ProgrammersManual.html
