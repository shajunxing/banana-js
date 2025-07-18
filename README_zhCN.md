# Banana Script，一个严格子集 JavaScript 的解释器

本文使用 [CC BY-NC-ND 4.0](https://creativecommons.org/licenses/by-nc-nd/4.0/) 许可。

[英文版](README.md) | [中文版](README_zhCN.md)

项目地址：<https://github.com/shajunxing/banana-script>

![REPL](screenshot1.png "REPL")

![命令行参数](screenshot2.png "命令行参数")

## 介绍

我的目标是剔除和修改我在实践中总结的JavaScript语言的没用的和模棱两可的部分，只保留我喜欢和需要的，创建一个最小语法的解释器。**只支持 JSON 兼容的数据类型和函数，函数是第一类值，函数支持闭包。我不喜欢面向对象编程，所以所有与类相关的内容都不支持**。没有任何内置不可删除的全局变量、全局函数或对象成员，哪怕解释器初始化的时候加入的内容都可以在任何时候轻松删除，恢复到空空如也的状态。

## 给JavaScript熟练者的两分钟简要语法指南

数值类型为`null` `boolean` `number` `string` `array` `object` `function`，`typeof`的结果严格对应这些名字。不支持 `undefined`，因为 `null` 已经足够。数组和对象是干净的，没有预定义的成员，比如`__proto__`。

变量声明使用 `let`，所有变量都是局部变量，不支持 `const`，因为一切都必须可删除。访问未声明的变量会引发错误，访问数组/对象不存在的成员会返回 `null`，写入`null`则为删除对应成员。

函数定义支持默认参数 `param = value` 和剩余参数 `...args`。数组字面量和函数调用支持展开语法 `...`，不会跳过`null`成员。函数中没有 预定义的成员比如`this` `arguments`。`return` 如果在函数外部，意为退出虚拟机。

运算符遵循严格规则，没有隐式转换。只有布尔值可以进行逻辑运算。`== !=` 是严格意义上的比较，可以应用于所有类型。字符串支持所有关系运算符和 `+`。数字支持所有关系和数值运算符。运算符的优先级从低到高为：

- 三元运算符 `?` `:`
- 逻辑或运算符 `||`
- 逻辑与运算符 `&&`
- 关系运算符 `==` `!=` `<` `<=` `>` `>=`
- 加减运算符 `+` `-`
- 乘除运算符 `*` `/` `%`
- 指数运算符 `**`
- 前缀运算符 `+` `-` `!` `typeof`
- 数组/对象成员访问和函数调用运算符 `[]` `.` `?.` `()`

赋值表达式 `=` `+=` `-=` `*=` `/=` `%=` `++` `--` 不返回值。不支持逗号表达式 `,`。

条件语句是`if`，循环语句是`while` `do while` `for`，条件必须是布尔值。`for` 循环仅支持以下语法，`[]` 表示可选部分。`for in` 和 `for of` 只处理非 `null` 的成员：

- `for ([[let] variable = expression ] ; [condition] ; [assignment expression])`
- `for ([let] variable in array/object)`
- `for ([let] variable of array/object)`

不支持模块。在解释器的视角中，源码只是一个大的平坦文本。

垃圾回收是手动的，你可以在你需要的任何时候执行。

`delete` 语义为删除当前作用域范围的局部变量（对象成员置`null`即可删除）。比如，加入函数闭包的变量是声明函数变量之前的所有局部变量，可以在返回之前`delete`掉无用的变量以减少闭包大小，在REPL环境里执行以下两条语句，可以看到区别。

- `let f = function(a, b){let c = a + b; return function(d){return c + d;};}(1, 2); dump(); print(f(3)); delete f;`
- `let f = function(a, b){let c = a + b; delete a; delete b; return function(d){return c + d;};}(1, 2); dump(); print(f(3)); delete f;`

`throw` 可以抛出任意值，由`catch`接收。不支持`finally`，因为我认为根本不需要，反而会使代码执行顺序显得怪异。

## 技术内幕

本项目兼容 C99，没有其他依赖，甚至不需要 make 系统，只需要 C 编译器，编译环境为 msvc/gcc/mingw。首先，从 <https://github.com/shajunxing/banana-nomake> 下载单独文件 `make.h`，然后打开 `make.c`，修改 `#include` 为正确的路径，然后使用 msvc 输入 `cl make.c && make.exe release`，或者使用 mingw 输入 `gcc -o make.exe make.c && ./make.exe release`。可执行文件位于 `bin` 文件夹中。

项目遵循“最小依赖”原则，只包含必须的头文件，且模块之间只有单向引用，没有循环引用。模块的功能和依赖关系如下：

```
    js-common   js-data     js-vm       js-syntax   js-std
        <-----------
        <-----------------------
                    <-----------
        <-----------------------------------
                                <-----------
        <-----------------------------------------------
                                <-----------------------
```

- `js-common`： 项目通用的常量、宏定义和函数，例如日志打印、内存读写
- `js-data`：数值类型和垃圾回收，你甚至可以在C项目里单独使用该模块操作带GC功能的高级数据结构，参见 <https://github.com/shajunxing/banana-cvar>
- `js-vm`：字节码虚拟机，单独编译可得到不带源代码解析功能的最小足迹的解释器
- `js-syntax`：词法解析和语法解析，将源代码转化为字节码
- `js-std`：一些常用标准函数的参考实现，注意只是用作编写C函数的参考，不保证将来会变，具体用法可参考我的另一个项目 <https://github.com/shajunxing/view-comic-here>

所有值都是 `struct js_value` 类型，你可以通过 `js_xxx()` 函数创建，`xxx` 是值类型，你可以直接从这个结构体中读取 C 值，参见 `js_data.h` 中的定义。创建的值遵循垃圾回收规则。不要直接修改它们，如果你想得到不同的值，就创建新值。复合类型 `array` `object` 可以通过 `js_array_xxx()` `js_object_xxx()` 函数进行操作。

C 函数必须是 `struct js_result (*)(struct js_vm *)` 格式，使用 `js_c_function()` 来创建 C 函数值，是的，当然它们都是值，可以放在任何地方，例如，如果使用 `js_declare_variable()` 放在堆栈根上，它们就是全局的。`struct js_result` 有两个成员，如果 `.success` 是 true, `.value` 就是返回值, 如果 false, `.value` 将会被 `catch` 接收，如果 `try catch` 存在的话。C函数同样也可以使用 `js_call()`调用脚本函数。在C函数内部，使用`js_get_arguments_base()` `js_get_arguments_length()` `js_get_argument()`函数获取传入参数。

## 其它详见英文版

