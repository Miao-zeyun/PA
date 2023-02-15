[toc]

# 1	概述

## 1.1	实验目的

1. 充分理解NEMU框架代码中有关执行指令和输入输出的部分
2. 充分理解AM(Abstract machine)和`nexus-am`目录下与PA2阶段相关的框架代码
3. 实现更高效的NEMU调试器(Differential Testing)
4. 加入IOE将NEMU补全为冯诺依曼计算机系统

## 1.2	实验内容

最简单的计算机"图灵机"(Turing Machine)的工作方式为：

![TRM](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\TRM.jpg)

但在PA1阶段的实验中并未接触取指、译码、执行和更新EIP的具体实现，也并未在NEMU中运行除默认镜像外的其他程序，但在PA2阶段的实验中，为了使NEMU能够运行其他的程序，必须了解NEMU的框架代码中执行指令的相关部分，通过阅读对应的框架代码，理解NEMU包括取指、译码、执行和更新EIP在内的执行指令的具体代码和实现方式。但NEMU仅提供（模拟）了执行程序所需的硬件，还需要软件系统来实现程序与NEMU之间的连接，所以还需要理解AM中有关程序编译、链接、加载、执行和结束等的具体代码。此外为了帮助在PA2阶段进行调试，还需要了解Differential Testing的工作原理。

在PA2部分需要实现Differential Testing、NEMU运行PA2阶段程序所需的全部指令和输入输出功能、AM中辅助NEMU进行输入输出的IOE接口并在NEMU上成功运行PA2阶段的全部程序。共包括三个阶段：

阶段一：实现`call`，`push`，`sub`，`xor`，`pop`和`ret`六条指令并在NEMU中成功运行客户程序`dummy`

阶段二：实现更多的指令并在NEMU中成功运行`nexus-am/tests/cputest/tests`目录下的所有测试程序；实现Differential Testing

阶段三：实现IOE并成功运行`nexus-am/apps`目录下的`hello`程序和`nexus-am/tests/timetest`、`nexus-am/tests/keytest`和`nexus-am/tests/videotest`目录下的测试程序

# 2	阶段一

NEMU执行一条指令的过程与真正的CPU相同，每个指令周期都包括取指(instruction fetch，IF)、译码(instruction decode，ID)，执行(execute，EX)和更新EIP四个步骤，为了帮助理解NEMU中实现指令执行的具体代码，先回顾下真实的CPU是如何执行指令的：

- 取指：将EIP所指向的指令从内存读入CPU
- 译码：CPU从指令中解读出具体的”操作码“和”操作数“
- 执行：CPU执行当前指令需要执行的工作，包括从寄存器堆取数、将源操作数送到ALU、将结果回写到目的操作数（寄存器或内存）等
- 更新EIP：EIP加上上条执行的指令的长度或是向EIP写入为需要跳转的指令地址

显然NEMU作为一款经过简化的x86全系统模拟器，执行指令的流程与真实的使用x86指令集的CPU是几乎一致的，牢记这一点对对应框架代码的理解非常有帮助。NEMU的框架代码中执行指令的关键部分如下：

**两个关键的数据结构**

- `nemu/src/cpu/decode/decode.c`文件中的`decoding`结构(在`nemu/include/cpu/decode.h`文件中定义)，记录全局译码信息供后续解码和执行过程使用，包括操作数的类型、宽度、值等信息，最多可记录两个源操作数和一个目的操作数（成员`src`，`src2`和`dest`，还定义了`id_src`，`id_src2`和`id_dest`宏便于后续过程中的访问）

  ```c
  typedef struct {
    uint32_t type;
    int width;
    union {
      uint32_t reg;
      rtlreg_t addr;
      uint32_t imm;
      int32_t simm;
    };
    rtlreg_t val;
    char str[OP_STR_SIZE];
  } Operand;
  
  typedef struct {
    uint32_t opcode;
    vaddr_t seq_eip;  // sequential eip
    bool is_operand_size_16;
    uint8_t ext_opcode;
    bool is_jmp;
    vaddr_t jmp_eip;
    Operand src, dest, src2;
  } DecodeInfo;
  
  extern DecodeInfo decoding;
  ```

- `nemu/src/cpu/exec/exec.c`文件中的`opcode_table`数组，NEMU在译码过程中的译码查找表，通过操作码`opcode`来索引，每一个`opcode`对应相应指令的译码函数，执行函数，以及操作数宽度，`opcode_table`数组中各指令的译码和执行函数的打包通过以下五个宏实现，操作数宽度的判断通过传入的参数进行判断，默认情况下宽度为2字节或4字节，此外双字节操作码和NEMU中用到的6个指令组合也已经用宏定义实现，指令组合中译码函数和操作数宽度NEMU框架代码中也已经指定。

  ```c
  #define IDEXW(id, ex, w)   {concat(decode_, id), concat(exec_, ex), w}
  #define IDEX(id, ex)       IDEXW(id, ex, 0)
  #define EXW(ex, w)         {NULL, concat(exec_, ex), w}
  #define EX(ex)             EXW(ex, 0)
  #define EMPTY              EX(inv)
  
  static inline void set_width(int width) {
    if (width == 0) {
      width = decoding.is_operand_size_16 ? 2 : 4;
    }
    decoding.src.width = decoding.dest.width = decoding.src2.width = width;
  }
  
  //双字节操作码
  static make_EHelper(2byte_esc);
  
  static make_EHelper(2byte_esc) {
    uint32_t opcode = instr_fetch(eip, 1) | 0x100;
    decoding.opcode = opcode;
    set_width(opcode_table[opcode].width);
    idex(eip, &opcode_table[opcode]);
  }
  
  //指令组合(group)
  #define make_group(name, item0, item1, item2, item3, item4, item5, item6, item7) \
    static opcode_entry concat(opcode_table_, name) [8] = { \
      /* 0x00 */	item0, item1, item2, item3, \
      /* 0x04 */	item4, item5, item6, item7  \
    }; \
  static make_EHelper(name) { \
    idex(eip, &concat(opcode_table_, name)[decoding.ext_opcode]); \
  }
  ```
  

**三类关键的宏定义函数**

- `nemu/include/cpu/exec.h`文件中的`make_EHelper(name)`，定义执行阶段的helper函数，所有`exec_name()`函数参数均为`(vaddr_t *eip)`

  ```c
  #define make_EHelper(name) void concat(exec_, name) (vaddr_t *eip)
  ```

- `nemu/include/cpu/decode.h`文件中的`make_DHelper(name)`，定义译码阶段的helper函数，所有`decode_name()`函数参数均为`(vaddr_t *eip)`

  ```c
  #define make_DHelper(name) void concat(decode_, name) (vaddr_t *eip)
  ```

- `nemu/src/cpu/decode/decode.c`文件中的`make_DopHelper(name)`，定义译码阶段操作数译码的helper函数，所有`decode_op_name()`函数参数均为`(vaddr_t *eip, Operand *op, bool load_val)`,（`decode_op_rm()`除外）

  ```c
  #define make_DopHelper(name) void concat(decode_op_, name) (vaddr_t *eip, Operand *op, bool load_val)
  ```

NEMU 通过不同的helper函数来模拟指令执行过程中不同的步骤。

此外这三类宏定义函数中的`concat()`在`nemu/include/macro.h`中定义：

```c
#define concat_temp(x, y) x ## y
#define concat(x, y) concat_temp(x, y)
#define concat3(x, y, z) concat(concat(x, y), z)
#define concat4(x, y, z, w) concat3(concat(x, y), z, w)
#define concat5(x, y, z, v, w) concat4(concat(x, y), z, v, w)
```

利用C语言宏定义中的`##`进行字符串拼接从而实现利用一个宏定义来定义一系列相同参数的函数。

**具体执行过程中的关键函数**

`nemu/src/cpu/exec/exec.c`文件中的`exec_wrapper()`函数：

```c
void exec_wrapper(bool print_flag) {
  decoding.seq_eip = cpu.eip; //将当前的eip保存到全局译码信息decoding的成员seq_eip中
  exec_real(&decoding.seq_eip); //将decoding.seq_eip作为参数调用exec_real()函数
  update_eip(); //更新eip
}
```

`exec_real()`函数：

```c
make_EHelper(real) {
  uint32_t opcode = instr_fetch(eip, 1); //取指,得到指令的第一个字节,解释为opcode
  decoding.opcode = opcode; //将opcode记录在全局译码信息decoding中
  set_width(opcode_table[opcode].width); //根据opcode查阅译码查找表,得到并设置操作数宽度
  idex(eip, &opcode_table[opcode]); //调用idex()函数对指令进行进一步的译码和执行
}
```

`idex()`函数：

```c
typedef struct {
  DHelper decode;
  EHelper execute;
  int width;
} opcode_entry;

/* Instruction Decode and EXecute */
static inline void idex(vaddr_t *eip, opcode_entry *e) {
  /* eip is pointing to the byte next to opcode */
  if (e->decode)
    e->decode(eip); //译码
  e->execute(eip); //执行
}
```

`update_eip()`函数：

```c
static inline void update_eip(void) {
  //decoding.is_jmp为true则将eip置为decoding.jmp_eip，否则置为decoding.seq_eip
  cpu.eip = (decoding.is_jmp ? (decoding.is_jmp = 0, decoding.jmp_eip) : decoding.seq_eip);
}
```

其中所有`DHelper`函数和除`make_DopHelper(SI)`外的`DopHelper`函数NEMU框架代码均已实现，且命名原则均符合i386手册附录A中的操作数表示记号，例如I2r表示将立即数移入寄存器，其中I表示立即数，2表示英文to，r表示通用寄存器，SI表示带符号立即数等。`DHelper`译码函数会把指令中的操作数信息分别记录在全局译码信息decoding中，这些译码函数会进一步分解成各种不同操作数的译码的组合，以实现操作数译码的解耦；`DopHelper`操作数译码函数会把操作数的信息记录在结构体`op`中，如果操作数在指令中，就通过`instr_fetch()`函数将它们从EIP所指向的内存位置取出。为了使操作数译码函数更易于复用，函数中的`load_val`参数会控制是否需要将该操作数读出到全局译码信息`decoding`供后续使用。

`idex()`函数中的译码过程结束之后，会调用译码查找表中的相应的`EHelper`执行函数来进行真正的执行操作，它们的名字就是指令操作本身。之后执行函数通过RTL来描述指令真正的执行功能，并通过`operand_write()`函数(`nemu/src/cpu/decode/decode.c`中定义)进行相应的回写操作，包括写寄存器和写内存。

**RTL**

RTL(Register Transfer Language)即寄存器传输语言，NEMU在指令的执行过程中调用RTL指令来执行具体的操作内容，为了使RTL指令能满足执行NEMU所需的x86指令，NEMU中定义了RTL寄存器、RTL指令（包括RTL基本指令和RTL伪指令，其中RTL伪指令通过RTL基本指令或者已经实现的RTL伪指令来实现）。其中RTL寄存器统一使用`rtlreg_t`来定义：

```c
typedef uint32_t rtlreg_t;
```

- RTL寄存器

  x86的八个通用寄存器(`nemu/include/cpu/reg.h`中定义)

  `id_src`，`id_src2`和`id_dest`中的访存地址`addr`和操作数内容`val`(在`nemu/include/cpu/decode.h`中定义)

  临时寄存器`t0`-`t3`(`nemu/src/cpu/decode/decode.c`中定义)

  0寄存器`tzero`(`nemu/src/cpu/decode/decode.c`中定义)，只能读出0，不能写入

- RTL基本指令(在`nemu/include/cpu/rtl.h`中定义)

  立即数读入`rtl_li`

  算术运算和逻辑运算,包括寄存器-寄存器类型`rtl_(add|sub|and|or|xor|shl|shr|sar|slt|sltu)`和立即
  数-寄存器类型`rtl_(add|sub|and|or|xor|shl|shr|sar|slt|sltu)i`

  内存的访存`rtl_lm`和`rtl_sm`

  通用寄存器的访问`rtl_lr_(b|w|l)`和`rtl_sr_(b|w|l)`

- RTL伪指令(在`nemu/include/cpu/rtl.h`中定义)

  带宽度的通用寄存器访问`rtl_lr`和`rtl_sr`

  EFLAGS标志位的读写`rtl_set_(CF|OF|ZF|SF|IF)`和`rtl_get_(CF|OF|ZF|SF|IF)`

  其它常用功能，如数据移动`rtl_mv`，符号扩展`rtl_sext`等

  （NEMU框架代码中仅实现了部分RTL指令）

跟随PA实验流程的指导，阅读PA对应框架代码，NEMU执行指令的具体过程到此全部清楚，开始添加具体的指令。在PA2阶段一需要在NEMU上运行的C程序为`nexus-am/tests/cputest/tests`目录下的`dummy`程序，查看`dummy`程序的反汇编结果即可知晓需要实现的具体指令，包括`call`，`push`，`sub`，`xor`，`pop`和`ret`共六条，将这六条指令成功实现后即可在NEMU上成功运行`dummy`程序。

此外NEMU框架代码为了保证默认镜像的运行，已经实现了部分指令，在实现其他代码的过程中可以参考已实现代码来实现其他指令。

## 2.1	实现方法及代码分析

### 2.1.1	CALL指令实现

在`nexus-am/tests/cputest`目录下键入`make ARCH=x86-nemu ALL=dummy run`编译`dummy`程序，并启动NEMU运行它。NEMU会输出报错信息，

![failure1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\failure1.jpg)

这意味着以0xe8为首字节的指令还没有实现，需要到i386手册的附录A的Opcode Map中查询e8对应位置的指令，

![Opcode_Map](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\Opcode_Map.jpg)

即CALL指令，再到i386手册17.2.2.11	Instruction Set Detail中查询CALL指令的具体细节，

![CALL1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CALL1.jpg)

即可知道e8对应的CALL指令的具体格式(CALL rel16/CALL rel32)，操作数类型(rel)，操作数宽度(2/4)和以伪代码形式表示的CALL指令具体操作，NEMU中只需实现CALL rel32的形式即可，

![CALL2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CALL2.jpg)

接下来进行e8位置的CALL指令的具体实现，参考NEMU执行指令的全部流程和已经实现的部分指令，可以知晓想要让NEMU能够成功执行一条指令，需要在至少三个位置进行修改，包括在`nemu/src/cpu/exec/exec.c`文件中的`opcode_table`数组中添加对应的指令，在`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(name)`的声明，在`nemu/src/cpu/exec`目录下找到所实现指令对应的分类`.c`文件并实现其执行函数`make_EHelper(name)`，因为所有的译码函数`make_DHelper(name)`在NEMU框架代码中均已实现，所以实现指令时只需要挑选合适的译码函数即可（译码函数名字和框架代码中的注释可以帮助理解译码函数对应功能并进行选择，选择时主要参考Opcode Map中指令对应位置的操作数格式）。倘若需要用到的译码函数中调用的操作数译码函数`make_DopHelper(name)`和执行函数中需要调用的RTL指令尚未实现，则还需要到`nemu/src/cpu/decode/decode.c`文件和`nemu/include/cpu/rtl.h`文件中实现所用到的操作数译码函数和RTL指令。

根据e8处CALL指令的具体格式，和NEMU中已实现的译码函数，选择`make_DHelper(J)`译码函数和`make_EHelper(call)`执行函数，操作数宽度为默认的4，所以打包好的CALL指令即为`IDEX(J, call)`。

在`opcode_table`数组中对应位置添加CALL指令（为减少报告中的重复代码，此处的`opcode_table`已包含PA2所有需实现指令），

```c
opcode_entry opcode_table [512] = {
  /* 0x00 */	IDEXW(G2E, add, 1), IDEX(G2E, add), IDEXW(E2G, add, 1), IDEX(E2G, add),
  /* 0x04 */	EMPTY, IDEX(I2a, add), EMPTY, EMPTY,
  /* 0x08 */	IDEXW(G2E, or, 1), IDEX(G2E, or), IDEXW(E2G, or, 1), IDEX(E2G, or),
  /* 0x0c */	EMPTY, IDEX(I2a, or), EMPTY, EX(2byte_esc),
  /* 0x10 */	IDEXW(G2E, adc, 1), IDEX(G2E, adc), IDEXW(E2G, adc, 1), IDEX(E2G, adc),
  /* 0x14 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x18 */	IDEXW(G2E, sbb, 1), IDEX(G2E, sbb), IDEXW(E2G, sbb, 1), IDEX(E2G, sbb),
  /* 0x1c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x20 */	IDEXW(G2E, and, 1), IDEX(G2E, and), IDEXW(E2G, and, 1), IDEX(E2G, and),
  /* 0x24 */	EMPTY, IDEX(I2a, and), EMPTY, EMPTY,
  /* 0x28 */	IDEXW(G2E, sub, 1), IDEX(G2E, sub), IDEXW(E2G, sub, 1), IDEX(E2G, sub),
  /* 0x2c */	EMPTY, IDEX(I2a, sub), EMPTY, EMPTY,
  /* 0x30 */	IDEXW(G2E, xor, 1), IDEX(G2E, xor), IDEXW(E2G, xor, 1), IDEX(E2G, xor),
  /* 0x34 */	EMPTY, IDEX(I2a, xor), EMPTY, EMPTY,
  /* 0x38 */	IDEXW(G2E, cmp, 1), IDEX(G2E, cmp), IDEXW(E2G, cmp, 1), IDEX(E2G, cmp),
  /* 0x3c */	IDEXW(I2a, cmp, 1), IDEX(I2a, cmp), EMPTY, EMPTY,
  /* 0x40 */	IDEX(r, inc), IDEX(r, inc), IDEX(r, inc), IDEX(r, inc),
  /* 0x44 */	IDEX(r, inc), IDEX(r, inc), IDEX(r, inc), IDEX(r, inc),
  /* 0x48 */	IDEX(r, dec), IDEX(r, dec), IDEX(r, dec), IDEX(r, dec),
  /* 0x4c */	IDEX(r, dec), IDEX(r, dec), IDEX(r, dec), IDEX(r, dec),
  /* 0x50 */	IDEX(r, push), IDEX(r, push), IDEX(r, push), IDEX(r, push),
  /* 0x54 */	IDEX(r, push), IDEX(r, push), IDEX(r, push), IDEX(r, push),
  /* 0x58 */	IDEX(r, pop), IDEX(r, pop), IDEX(r, pop), IDEX(r, pop),
  /* 0x5c */	IDEX(r, pop), IDEX(r, pop), IDEX(r, pop), IDEX(r, pop),
  /* 0x60 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x64 */	EMPTY, EMPTY, EX(operand_size), EMPTY,
  /* 0x68 */	IDEX(I, push), EMPTY, IDEXW(push_SI, push, 1), EMPTY,
  /* 0x6c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x70 */	IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1),
  /* 0x74 */	IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1),
  /* 0x78 */	IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1),
  /* 0x7c */	IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1), IDEXW(J, jcc, 1),
  /* 0x80 */	IDEXW(I2E, gp1, 1), IDEX(I2E, gp1), EMPTY, IDEX(SI2E, gp1),
  /* 0x84 */	IDEXW(G2E, test, 1), IDEX(G2E, test), EMPTY, EMPTY,
  /* 0x88 */	IDEXW(mov_G2E, mov, 1), IDEX(mov_G2E, mov), IDEXW(mov_E2G, mov, 1), IDEX(mov_E2G, mov),
  /* 0x8c */	EMPTY, IDEX(lea_M2G, lea), EMPTY, EMPTY,
  /* 0x90 */	EX(nop), EMPTY, EMPTY, EMPTY,
  /* 0x94 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x98 */	EX(cwtl), EX(cltd), EMPTY, EMPTY,
  /* 0x9c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xa0 */	IDEXW(O2a, mov, 1), IDEX(O2a, mov), IDEXW(a2O, mov, 1), IDEX(a2O, mov),
  /* 0xa4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xa8 */	IDEXW(I2a, test, 1), EMPTY, EMPTY, EMPTY,
  /* 0xac */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xb0 */	IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1),
  /* 0xb4 */	IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1), IDEXW(mov_I2r, mov, 1),
  /* 0xb8 */	IDEX(mov_I2r, mov), IDEX(mov_I2r, mov), IDEX(mov_I2r, mov), IDEX(mov_I2r, mov),
  /* 0xbc */	IDEX(mov_I2r, mov), IDEX(mov_I2r, mov), IDEX(mov_I2r, mov), IDEX(mov_I2r, mov),
  /* 0xc0 */	IDEXW(gp2_Ib2E, gp2, 1), IDEX(gp2_Ib2E, gp2), EMPTY, EX(ret),
  /* 0xc4 */	EMPTY, EMPTY, IDEXW(mov_I2E, mov, 1), IDEX(mov_I2E, mov),
  /* 0xc8 */	EMPTY, EX(leave), EMPTY, EMPTY,
  /* 0xcc */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xd0 */	IDEXW(gp2_1_E, gp2, 1), IDEX(gp2_1_E, gp2), IDEXW(gp2_cl2E, gp2, 1), IDEX(gp2_cl2E, gp2),
  /* 0xd4 */	EMPTY, EMPTY, EX(nemu_trap), EMPTY,
  /* 0xd8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xdc */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe8 */	IDEX(J, call), IDEX(J, jmp), EMPTY, IDEXW(J, jmp, 1),
  /* 0xec */	IDEXW(in_dx2a, in, 1), IDEX(in_dx2a, in), IDEXW(out_a2dx, out, 1), EMPTY,
  /* 0xf0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xf4 */	EMPTY, EMPTY, IDEXW(E, gp3, 1), IDEX(E, gp3),
  /* 0xf8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xfc */	EMPTY, EMPTY, IDEXW(E, gp4, 1), IDEX(E, gp5),

  /*2 byte_opcode_table */

  /* 0x00 */	EMPTY, IDEX(gp7_E, gp7), EMPTY, EMPTY,
  /* 0x04 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x08 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x0c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x10 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x14 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x18 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x1c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x20 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x24 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x28 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x2c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x30 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x34 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x38 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x3c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x40 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x44 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x48 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x4c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x50 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x54 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x58 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x5c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x60 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x64 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x68 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x6c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x70 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x74 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x78 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x7c */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0x80 */	IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc),
  /* 0x84 */	IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc),
  /* 0x88 */	IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc),
  /* 0x8c */	IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc), IDEX(J, jcc),
  /* 0x90 */	IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1),
  /* 0x94 */	IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1),
  /* 0x98 */	IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1),
  /* 0x9c */	IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1), IDEXW(E, setcc, 1),
  /* 0xa0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xa4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xa8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xac */	EMPTY, EMPTY, EMPTY, IDEX(E2G, imul2),
  /* 0xb0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xb4 */	EMPTY, EMPTY, IDEXW(mov_E2G, movzx, 1), IDEXW(mov_E2G, movzx, 2),
  /* 0xb8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xbc */	EMPTY, EMPTY, IDEXW(mov_E2G, movsx, 1), IDEXW(mov_E2G, movsx, 2),
  /* 0xc0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xc4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xc8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xcc */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xd0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xd4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xd8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xdc */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xe8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xec */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xf0 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xf4 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xf8 */	EMPTY, EMPTY, EMPTY, EMPTY,
  /* 0xfc */	EMPTY, EMPTY, EMPTY, EMPTY
};
```

`make_DHelper(J)`译码函数中调用的操作数译码函数`decode_op_SI()`尚未实现，

```c
make_DHelper(J) {
  decode_op_SI(eip, id_dest, false);
  // the target address can be computed in the decode stage
  decoding.jmp_eip = id_dest->simm + *eip;
}
```

所以需要先实现这一从操作数中读取立即数的操作数译码函数，此处参考NEMU框架代码给出的注释进行实现，

```c
static inline make_DopHelper(SI) {
  assert(op->width == 1 || op->width == 4);
  op->type = OP_TYPE_IMM;
  /* TODO: Use instr_fetch() to read `op->width' bytes of memory
   * pointed by `eip'. Interpret the result as a signed immediate,
   * and assign it to op->simm.
   *
   op->simm = ???
   */
  //从框架代码中可以知晓只需考虑操作数宽度为1和宽度为4的情况，计算机处理无符号数的带符号数的区别在于读取，而指令中传入的为带符号立即数，op->simm成员类型为int32_t，宽度为4，所以只需要额外处理操作数宽度与op->simm宽度不同的情况，因为将宽度为1的带符号立即数转化为宽度为4的带符号立即数要注意进行符号扩展
  if (op->width==1)
    op->simm=(int8_t)instr_fetch(eip, op->width); //若操作数宽度为1，则先转化为int8_t类型再赋值
  else
    op->simm=instr_fetch(eip, op->width); //若操作数宽度为4，则直接赋值即可
  rtl_li(&op->val, op->simm);
}
```

因为先前在阅读框架代码的过程中已经注意到未实现的操作数译码函数仅有`decode_op_SI()`，所以在之后的指令实现过程中就不必再关注是否使用的是未实现的操作数译码函数了。

在`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(call)`的声明（为减少报告中的重复代码，此处的`all-instr.h`文件已包含PA2所有需实现指令），

```c
//prefix.c
make_EHelper(operand_size);

//special.c
make_EHelper(inv);
make_EHelper(nemu_trap);
make_EHelper(nop);

//control.c
make_EHelper(call);
make_EHelper(ret);
make_EHelper(jcc);
make_EHelper(jmp);
make_EHelper(call_rm);
make_EHelper(jmp_rm);

//data-mov.c
make_EHelper(mov);
make_EHelper(push);
make_EHelper(pop);
make_EHelper(lea);
make_EHelper(movzx);
make_EHelper(cltd);
make_EHelper(movsx);
make_EHelper(leave);
make_EHelper(cwtl);

//arith.c
make_EHelper(sub);
make_EHelper(add);
make_EHelper(cmp);
make_EHelper(adc);
make_EHelper(dec);
make_EHelper(inc);
make_EHelper(imul2);
make_EHelper(idiv);
make_EHelper(imul1);
make_EHelper(sbb);
make_EHelper(div);
make_EHelper(mul);
make_EHelper(neg);

//logic.c
make_EHelper(xor);
make_EHelper(and);
make_EHelper(setcc);
make_EHelper(test);
make_EHelper(or);
make_EHelper(sar);
make_EHelper(shl);
make_EHelper(not);
make_EHelper(shr);
make_EHelper(rol);

//system.c
make_EHelper(out);
make_EHelper(in);
```

在`nemu/src/cpu/exec/control.c`文件中实现CALL指令执行函数`make_EHelper(call)`，需要实现具体操作参考i386手册，`decoding.jmp_eip`已经在`make_DHelper(J)`译码函数中获得，

```c
make_EHelper(call) {
  // the target address is calculated at the decode stage
  rtl_push(&decoding.seq_eip); //decoding.seq_eip入栈
  decoding.is_jmp=1; //跳转标志decoding.is_jmp置为1
  print_asm("call %x", decoding.jmp_eip);
}
```

需要使用的`rtl_push`指令尚未实现，所以还需要到`nemu/include/cpu/rtl.h`文件中实现`rtl_push`指令（RTL指令的实现参考NEMU框架代码中注释即可），

```c
static inline void rtl_push(const rtlreg_t* src1) {
  // esp <- esp - 4
  // M[esp] <- src1
  rtl_subi(&cpu.esp,&cpu.esp,4);
  rtl_sm(&cpu.esp,4,src1);
}
```

之后再启动NEMU运行`dummy`程序，NEMU会输出报错信息，

![failure2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\failure2.jpg)

意味着0xe8为首字节的指令已经被成功读取，而0x55为首字节的指令尚未实现（但这不一定意味着CALL指令已经成功实现，可能指令的执行阶段还存在着尚未被发现的Bug）。以上就是实现一条指令所需的全部工作，其他指令的实现基本按照以上流程即可，在报告中会简略其他指令的实现中与CALL指令相同的部分。

### 2.1.2	PUSH指令实现

查询PUSH指令具体细节，

![PUSH1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\PUSH1.jpg)

50+/r为首字节的PUSH的具体格式(PUSH r16/PUSH r32)，操作数类型(r)，操作数宽度(2/4)和以伪代码形式表示的PUSH指令具体操作，其中/r通用寄存器的编号顺序，按照i386手册中对八个通用寄存器的编号顺序，即从0-7分别为EAX，ECX，EDX，EBX，ESP，EBP，ESI，EDI，NEMU中只需实现PUSH r32的形式即可，

![PUSH2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\PUSH2.jpg)

根据55处PUSH指令的具体格式，和NEMU中已实现的译码函数，选择`make_DHelper(r)`译码函数和`make_EHelper(push)`执行函数，操作数宽度为默认的4，所以打包好的PUSH指令即为`IDEX(r, push)`，在`opcode_table`数组中对应位置添加PUSH指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(push)`的声明，最后实现对应的执行函数。

```c
make_DHelper(r) {
  decode_op_r(eip, id_dest, true);
}

make_EHelper(push) {
  rtl_push(&id_dest->val); //rtl_push已经实现，此处直接调用
  print_asm_template1(push);
}
```

### 2.1.3	SUB指令实现

在实现SUB指令之前，需要先实现EFLAGS寄存器，在寄存器结构体中添加EFLAGS寄存器即可，EFLAGS是一个32 位寄存器，结构如下：

![EFLAGS](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\EFLAGS.jpg)

NEMU中只会用到EFLAGS中的5个位：CF，ZF，SF，IF，OF，其他的位置空即可，EFLGAS的初值为0x00000002，在`restart()`函数(`nemu/src/monitor.monitor.c`文件中定义)中对EFLAGS寄存器进行初始化，

```c
union
  {
    struct
    {
      uint32_t    :20;
      uint32_t OF :1;
      uint32_t    :1;
      uint32_t IF :1;
      uint32_t    :1;
      uint32_t SF :1;
      uint32_t ZF :1;
      uint32_t    :5;
      uint32_t CF :1;
    } eflags;
    
    rtlreg_t eflags_init; //用于EFLAGS寄存器的初始化
  };


#define EFLAGS_START 0x00000002
static inline void restart() {
  /* Set the initial instruction pointer. */
  cpu.eip = ENTRY_START;
  cpu.eflags_init = EFLAGS_START;
}
```

Opcode Map中查询83对应位置的指令会发现是Grp 1，意味着在0x83后的0xec是一个ModR/M字节，格式为：

![ModR_M](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ModR_M.jpg)

其中Mod表示寄存器的寻址方式，Reg/opcode表示寄存器或者Opcode的编码，R/M表示汇编中第一个寄存器的编码。

- 当Mod = 00时，ModR/M字节通过寄存器直接进行内存寻址
- 当Mod = 01时，ModR/M字节通过寄存器+I8进行内存寻址(I为立即数，即8位立即数)
- 当Mod = 10时，ModR/M字节通过寄存器+I32进行内存寻址
- 当Mod = 11时，ModR/M字节直接访问寄存器内容

![ModR_MTable](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ModR_MTable.jpg)

所以这条指令的Opcode需要查询Group 1得出

![ModR_M5_3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ModR_M5_3.jpg)

0xec转换为二进制为1110 1100，意味着Mod=11，Opcode=101，是SUB指令且直接操作寄存器。

查询SUB指令具体细节，

![SUB1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SUB1.jpg)

83 /5为首字节的SUN的具体格式(SUB r/m16，imm8/SUB r/m32，imm8)，操作数类型(r/m16/r/m8，imm8)，操作数宽度(2/4，1)和以伪代码形式表示的SUB指令具体操作，通用寄存器的编号顺序见上文中Table 17-3，这里NEMU框架代码中已经填写好了`opcode_table`数组并选择了合适的译码函数`make_DHelper(SI2E)`，所以只需要选择对应的执行函数`make_EHelper(sub)`，操作数宽度为默认的2/4，所以打包好的group1内的SUB指令为`EX(sub)`，在gp1对应位置填入（为减少报告中的重复代码，此处的六个group已包含PA2所有需实现指令），

```c
/* 0x80, 0x81, 0x83 */
make_group(gp1,
    EX(add), EX(or), EMPTY/*EX(adc)*/, EX(sbb),
    EX(and), EX(sub), EX(xor), EX(cmp))

  /* 0xc0, 0xc1, 0xd0, 0xd1, 0xd2, 0xd3 */
make_group(gp2,
    EX(rol), EMPTY, EMPTY, EMPTY,
    EX(shl), EX(shr), EMPTY, EX(sar))

  /* 0xf6, 0xf7 */
make_group(gp3,
    IDEX(test_I, test), EMPTY, EX(not), EX(neg),
    EX(mul), EX(imul1), EX(div), EX(idiv))

  /* 0xfe */
make_group(gp4,
    EMPTY/*EXW(inc, 1)*/, EXW(dec, 1), EMPTY, EMPTY,
    EMPTY, EMPTY, EMPTY, EMPTY)

  /* 0xff */
make_group(gp5,
    EX(inc), EX(dec), EX(call_rm), EMPTY,
    EX(jmp_rm), EMPTY, EX(push), EMPTY)

  /* 0x0f 0x01*/
make_group(gp7,
    EMPTY, EMPTY, EMPTY, EMPTY,
    EMPTY, EMPTY, EMPTY, EMPTY)
```

再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(sub)`的声明，最后实现对应的执行函数，这里还要注意到SUB指令会影响EFLAGS寄存器，

![FlagsAffect](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\FlagsAffect.jpg)

![FlagsAffect1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\FlagsAffect1.jpg)

![FlagsAffect2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\FlagsAffect2.jpg)

可以看到SUB指令需要更新OF，SF，ZF，CF标志位，所以需要先完成RTL指令中有关EFLAGS寄存器标志位更新的指令，具体RTL指令的实现按照NEMU框架代码中的提示进行实现即可，

```c
#define eflags_CF (cpu.eflags.CF)
#define eflags_OF (cpu.eflags.OF)
#define eflags_ZF (cpu.eflags.ZF)
#define eflags_SF (cpu.eflags.SF)

#define make_rtl_setget_eflags(f) \
  static inline void concat(rtl_set_, f) (const rtlreg_t* src) { \
    concat(eflags_,f) = *src & 0x00000001; \
  } \
  static inline void concat(rtl_get_, f) (rtlreg_t* dest) { \
    *dest = concat(eflags_,f); \
  }

make_rtl_setget_eflags(CF)
make_rtl_setget_eflags(OF)
make_rtl_setget_eflags(ZF)
make_rtl_setget_eflags(SF)

static inline void rtl_msb(rtlreg_t* dest, const rtlreg_t* src1, int width) {
  // dest <- src1[width * 8 - 1]
  rtlreg_t msb=0;
  switch (width)
  {
    case 1:
      msb=*src1 & 0x00000080;
      break;
    case 2:
      msb=*src1 & 0x00008000;
      break;
    case 4:
      msb=*src1 & 0x80000000;
      break;
  }
  *dest=(msb>0 ? 1 : 0);
}

static inline void rtl_update_ZF(const rtlreg_t* result, int width) {
  // eflags.ZF <- is_zero(result[width * 8 - 1 .. 0])
  rtlreg_t zero_flag=0;
  switch (width)
  {
    case 1:
      zero_flag=*result & 0x000000ff;
      break;
    case 2:
      zero_flag=*result & 0x0000ffff;
      break;
    case 4:
      zero_flag=*result & 0xffffffff;
      break;
  }
  zero_flag=(zero_flag==0 ? 1 : 0);
  rtl_set_ZF(&zero_flag);
}

static inline void rtl_update_SF(const rtlreg_t* result, int width) {
  // eflags.SF <- is_sign(result[width * 8 - 1 .. 0])
  rtlreg_t sign_flag=0;
  rtl_msb(&sign_flag,result,width);
  sign_flag=(sign_flag==1 ? 1 : 0);
  rtl_set_SF(&sign_flag);
}

static inline void rtl_update_ZFSF(const rtlreg_t* result, int width) {
  rtl_update_ZF(result, width);
  rtl_update_SF(result, width);
}
```

实现对应的RTL指令后即可进行调用实现SUB指令执行函数`make_EHelper(sub)`，其中符号位的更新可参考已实现的SBB指令。

```c
make_DHelper(SI2E) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_dest, true, NULL, false); //ModR/M字节译码，目的寄存器存到id_dest
  id_src->width = 1;
  decode_op_SI(eip, id_src, true); //1位带符号立即数译码，值存到id_src->val
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}

make_EHelper(sub) {
  rtl_sub(&t2, &id_dest->val, &id_src->val); //rtl_sub已经实现，此处直接调用
  operand_write(id_dest, &t2); //结果回写

  rtl_update_ZFSF(&t2, id_dest->width); //ZF,SF更新

  rtl_sltu(&t0, &id_dest->val, &t2);
  rtl_set_CF(&t0); //CF更新

  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0); //OF更新

  //printf("sub 0x%08x to 0x%08x\n",id_src->val, id_dest->val);
  //rtl_get_CF(&t0);printf("CF: %d ,",t0);
  //rtl_get_OF(&t0);printf("OF: %d ,",t0);
  //rtl_get_ZF(&t0);printf("ZF: %d ,",t0);
  //rtl_get_SF(&t0);printf("SF: %d\n",t0);
  
  print_asm_template2(sub);
}
```

### 2.1.4	XOR指令实现

查询XOR指令具体细节，

![XOR1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\XOR1.jpg)

31为首字节的XOR的具体格式(XOR/m16，r16/XOR/m32，r32)，操作数类型(r/m和r)，操作数宽度(2/4)和以伪代码形式表示的XOR指令具体操作，

![XOR2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\XOR2.jpg)

根据31处XOR指令的具体格式，和NEMU中已实现的译码函数，选择`make_DHelper(G2E)`译码函数和`make_EHelper(xor)`执行函数，操作数宽度为默认的4，所以打包好的XOR指令即为`IDEX(G2E, xor)`，在`opcode_table`数组中对应位置添加XOR指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(xor)`的声明，最后实现对应的执行函数。

```c
make_DHelper(G2E) {
  //ModR/M字节译码，源寄存器和目的寄存器分别存到id_src和id_dest
  decode_op_rm(eip, id_dest, true, id_src, true);
}

make_EHelper(xor) {
  rtl_xor(&t0, &id_dest->val, &id_src->val); //rtl_xor指令已经实现，这里直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_set_CF(&tzero); //CF置为0
  rtl_set_OF(&tzero); //OF置为0
  rtl_update_ZFSF(&t0, id_dest->width);  //ZF,SF更新

  print_asm_template2(xor);
}
```

### 2.1.5	POP指令实现

查询POP指令具体细节，

![POP1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\POP1.jpg)

58+rw和58+rd为首字节的POP的具体格式(POP r16/POP r32)，操作数类型(r)，操作数宽度(2/4)和以伪代码形式表示的POP指令具体操作，通用寄存器的编号顺序相同，NEMU中只需实现POP r32的形式即可，

![POP2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\POP2.jpg)

根据5d处POP指令的具体格式，和NEMU中已实现的译码函数，选择`make_DHelper(r)`译码函数和`make_EHelper(pop)`执行函数，操作数宽度为默认的4，所以打包好的POP指令即为`IDEX(r, pop)`，在`opcode_table`数组中对应位置添加POP指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(pop)`的声明，这里还需要实现需要使用到的`rtl_pop`指令，

```c
static inline void rtl_pop(rtlreg_t* dest) {
  // dest <- M[esp]
  // esp <- esp + 4
  rtl_lm(dest,&cpu.esp,4);
  rtl_addi(&cpu.esp,&cpu.esp,4);
}
```

最后实现对应的执行函数。

```c
make_DHelper(r) {
  decode_op_r(eip, id_dest, true);
}

make_EHelper(pop) {
  rtl_pop(&t0); //调用rtl_pop指令
  operand_write(id_dest,&t0); //结果回写

  print_asm_template1(pop);
}
```

### 2.1.6	RET指令实现

查询RET指令具体细节，

![RET1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\RET1.jpg)

31为首字节的RET的具体格式(RET)，无具体操作数和以伪代码形式表示的RET指令具体操作，

![RET2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\RET2.jpg)

根据31处RET指令的具体格式，发现RET指令不需要译码函数，所以只需要选择`make_EHelper(ret)`执行函数，打包好的RET指令即为`EX(ret)`，在`opcode_table`数组中对应位置添加RET指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(ret)`的声明，最后实现对应的执行函数。

```c
make_EHelper(ret) {
  rtl_pop(&decoding.jmp_eip); //rtl_pop指令已经实现，此处直接调用
  decoding.is_jmp=1; //跳转标志decoding.is_jmp置为1

  print_asm("ret");
}
```

## 2.2	运行结果

成功在NEMU上运行`nexus-am/tests/cputest`目录下`dummy`程序。

![dummy](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\dummy.jpg)

## 2.3	Bug总结

阶段一Bug主要在于具体指令的实现和忘记在`nemu/src/cpu/exec/all-instr.h`文件声明执行函数。具体指令的实现的Bug主要出现在译码函数的选择和执行函数的实现过程，此外还需要注意的就是SUB指令的实现过程中OF，SF，ZF，CF标志位的更新，RTL指令的实现和SUB指令执行函数的实现过程中都有可能出错，这里可以通过打印源操作数、目的操作数以及更新前后的标志位来进行检查。

不过还是有可能出现实现的指令中有Bug但当下并未报错的情况，这会给Bug的定位带来比较大的麻烦，所以尽早实现Differential Testing还是非常有必要的。

# 3	阶段二

阶段二主要需要完成的工作是实现更多的指令来运行`nexus-am/tests/cputest/tests`目录下的所有测试程序，此外还要根据需要完成Differential Testing来帮助进行指令实现过程中的调试。其中指令的实现与阶段一中的流程基本一致，在阶段二的报告中就不再赘述，为了减少重复内容，阶段二中指令实现过程只保留重点，且根据指令种类分类，而非具体实现过程中的指令实现顺序（部分指令在实验过程中是在阶段三的验证程序中发现尚未实现并实现的，但报告中把这些指令的实现写在此阶段中）报告中的指令顺序按照PA实验流程中“你还需要实现更多的指令：”部分的顺序；Differential Testing通过让NEMU和QEMU逐条指令地执行同一个客户程序，双方每执行完一条指令，就检查各自的寄存器和内存的状态，如果发现状态不一致，就马上报告错误的方式来检查NEMU运行过程中的错误，具体的框架代码NEMU已经实现，只需要在`difftest_step()`函数(在`nemu/src/monitor/diff-test/diff-test.c`中定义)中完成寄存器内容的比较和在寄存器内容不一致的情况下中断程序执行并打印报错信息即可，实现了此功能后在`nemu/include/common.h`中定义宏DIFF_TEST即可启动Differential Testing。

## 3.1	实现方法及代码分析

因为需要实现的指令使用相同的译码函数和执行函数，仅操作数宽度不同（或操作数宽度也相同），且在Opcode Map中位置也相邻，例如：0x55-0x57的PUSH指令，0xb0-0xb7的MOV指令等。所以在具体实验的过程中验证了其中一条指令的正确性后为了减少重复工作直接将其他同类指令填写在`opcode_table`数组中，所以报告中包含的已实现指令可能会多于PA2所需要指令。

### 3.1.1	实现更多指令

#### 3.1.1.1	Data Movement Instructions

**MOV指令**

0x88-0x8b，0xa0-0xa3，0xb0-0xbf，0xc6-0xc7处；PA2所需MOV指令NEMU框架代码中均已实现。

![MOV1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOV1.jpg)

![MOV2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOV2.jpg)

**PUSH指令**

0x50-0x57，0x68，0x6a和0xff/6（group5中的110）处，`make_EHelper(push)`执行函数在阶段一已经实现，所以只需要根据指令格式选择对应译码函数和操作数宽度即可，查询PUSH指令具体细节，

![PUSH1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\PUSH1.jpg)

![PUSH2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\PUSH2.jpg)

其中0x50-0x57处PUSH指令格式为PUSH r16/r32，译码函数选择`make_DHelper(r)`，操作数宽度为默认的2/4，指令打包为`IDEX(r, push)`；0x68处PUSH指令格式为PUSH imm16/imm32，译码函数选择`make_DHelper(I)`操作数宽度为默认的2/4,指令打包为`IDEX(I, push)`；0x6a处PUSH指令格式为PUSH imm8，操作数宽度为1，且需要做符号扩展，所以译码函数选择`make_DHelper(push_SI)`，指令打包为`IDEX(push-SI, push, 1)`，0xff/6（group5中的110）处PUSH指令格式为PUSH m16/m32，译码函数和操作数宽度NEMU框架代码已经指定，其中译码函数为`make_DHelper(E)`，操作数宽度为默认的2/4，指令打包为`EX(push)`；再在`opcode_table`数组对应位置填写打包好的PUSH指令即可。

**POP指令**

0x58-0x5f处，`make_EHelper(pop)`执行函数在阶段一已经实现，所以只需要根据指令格式选择对应译码函数和操作数宽度即可，查询POP指令具体细节，

![POP1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\POP1.jpg)

![POP2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\POP2.jpg)

其中0x58-0x5f处POP指令格式为POP r16/r32，译码函数选择`make_DHelper(r)`，操作数宽度为默认的2/4，指令打包为`IDEX(r, pop)`，再在`opcode_table`数组对应位置填写打包好的POP指令即可。

**LEAVE指令**

0xc9处，查询LEAVE指令的具体细节，

![LEAVE1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\LEAVE1.jpg)

![LEAVE2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\LEAVE2.jpg)

不需要译码函数，也无具体操作数，只需要选择`make_EHelper(leave)`执行函数，指令打包为EX`(leave)`，在`opcode_table`数组中对应位置添加LEAVE指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(leave)`的声明，最后实现对应的执行函数。

```c
make_EHelper(leave) {
  cpu.esp=cpu.ebp;
  rtl_pop(&cpu.ebp); //rtl_pop指令已经实现，此处直接调用

  print_asm("leave");
}
```

**CLTD(CDQ)指令**

0x99处，查询CLTD(CDQ)指令的具体细节，

![CDQ1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CDQ1.jpg)

![CDQ2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CDQ2.jpg)

不需要译码函数，也无具体操作数，只需要选择`make_EHelper(cltd)`执行函数，指令打包为EX`(cltd)`，在`opcode_table`数组中对应位置添加CLTD指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(cltd)`的声明，最后实现对应的执行函数。

```c
make_EHelper(cltd) {
  if (decoding.is_operand_size_16)
  {
    rtl_lr(&t0,R_AX,2); //rtl_lr指令已经实现，此处直接调用，加载AX寄存器内容
    rtl_msb(&t1,&t0,2); //rtl_msb指令已经实现，此处直接调用，取最高有效位
    if (t1==1)
    {
      rtl_addi(&t2,&tzero,0xffff);
      rtl_sr(R_DX,2,&t2); //DX <- 0xffff
    }
    else
      rtl_sr(R_DX,2,&tzero); //DX <- 0
  }
  else
  {
    rtl_lr(&t0,R_EAX,4); //rtl_lr指令已经实现，此处直接调用，加载EAX寄存器内容
    rtl_msb(&t1,&t0,4); //rtl_msb指令已经实现，此处直接调用，取最高有效位
    if (t1==1)
    {
      rtl_addi(&t2,&tzero,0xffffffff);
      rtl_sr(R_EDX,4,&t2); //EDX <- 0xffff
    }
    else
      rtl_sr(R_EDX,4,&tzero); //EDX <- 0
  }

  print_asm(decoding.is_operand_size_16 ? "cwtl" : "cltd");
}
```

**CWTL(CWDE)指令**

0x98处，查询CWTL(CWDE)指令的具体细节，

![CWDE1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CWDE1.jpg)

![CWDE2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CWDE2.jpg)

不需要译码函数，也无具体操作数，只需要选择`make_EHelper(cwtl)`执行函数，指令打包为EX`(cwtl)`，在`opcode_table`数组中对应位置添加CWTL指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(cwtl)`的声明，这里还需要实现需要使用到的`rtl_sext`指令，

```c
static inline void rtl_sext(rtlreg_t* dest, const rtlreg_t* src1, int width) {
  // dest <- signext(src1[(width * 8 - 1) .. 0])
  switch(width)
  {
    case 1:
      *dest=(int32_t)(int8_t)*src1;
      break;
    case 2:
      *dest=(int32_t)(int16_t)*src1;
      break;
    case 4:
      *dest=(int32_t)*src1;
      break;
  }
  return;
}
```

最后实现对应的执行函数。

```c
make_EHelper(cwtl) {
  if (decoding.is_operand_size_16) {
    rtl_lr(&t0,R_AL,1); //rtl_lr指令已经实现，此处直接调用，加载AL寄存器内容
    rtl_sext(&t1,&t0,1); //rtl_sext指令已经实现，此处直接调用，做符号扩展
    rtl_sr(R_AX,2,&t1); //符号扩展后内容写入AX寄存器
  }
  else {
    rtl_lr(&t0,R_AX,2); //rtl_lr指令已经实现，此处直接调用，加载AX寄存器内容
    rtl_sext(&t1,&t0,2); //rtl_sext指令已经实现，此处直接调用，做符号扩展
    rtl_sr(R_EAX,4,&t0); //符号扩展后内容写入EAX寄存器
  }

  print_asm(decoding.is_operand_size_16 ? "cbtw" : "cwtl");
}
```

**MOVSX指令**

Two-Byte Opcode Map0x0f be，0x0f bf处，是双字节操作码，NEMU框架中已经将双字节操作码与单字节操作码组合到`opcode_table`数组中，读取的判断也已经实现，所以实现具体指令步骤和单字节操作码一致。查询MOVSX指令具体细节，

![MOVSX1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOVSX1.jpg)

![MOVSX2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOVSX2.jpg)

其中0x0f be处MOVSX指令格式为MOVSX r16/r32 r/m8，译码函数选择`make_DHelper(mov_E2G)`，执行函数选择`make_EHelper(movsx)`，操作数宽度为1，指令打包为`IDEXW(mov_E2G, movsx, 1)`；0x0f bf处MOVSX指令格式为MOVSX r32 r/m16，译码函数选择`make_DHelper(mov_E2G)`，执行函数选择`make_EHelper(movsx)`，操作数宽度为2，指令打包为`IDEXW(mov_E2G, movsx, 2)`，因为执行函数`make_EHelper(movsx)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(movsx)`的声明并在`opcode_table`数组对应位置填写打包好的MOVSX指令即可。

```c
make_DHelper(mov_E2G) {
  decode_op_rm(eip, id_src, true, id_dest, false);
}

make_EHelper(movsx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  rtl_sext(&t2, &id_src->val, id_src->width);
  operand_write(id_dest, &t2);
  print_asm_template2(movsx);
}
```

**MOVZX指令**

Two-Byte Opcode Map0x0f b6，0x0f b7处，是双字节操作码，查询MOVZX指令具体细节，

![MOVSX1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOVZX1.jpg)

![MOVSX2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MOVZX2.jpg)

其中0x0f be处MOVZX指令格式为MOVZX r16/r32 r/m8，译码函数选择`make_DHelper(mov_E2G)`，执行函数选择`make_EHelper(movzx)`，操作数宽度为1，指令打包为`IDEXW(mov_E2G, movzx, 1)`；0x0f bf处MOVZX指令格式为MOVZX r32 r/m16，译码函数选择`make_DHelper(mov_E2G)`，执行函数选择`make_EHelper(movzx)`，操作数宽度为2，指令打包为`IDEXW(mov_E2G, movzx, 2)`，因为执行函数`make_EHelper(movzx)`NEMU框架代码中已经实现，所以只到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(movzx)`的声明并在`opcode_table`数组对应位置填写打包好的MOVZX指令即可。

```c
make_DHelper(mov_E2G) {
  decode_op_rm(eip, id_src, true, id_dest, false);
}

make_EHelper(movzx) {
  id_dest->width = decoding.is_operand_size_16 ? 2 : 4;
  operand_write(id_dest, &id_src->val);
  print_asm_template2(movzx);
}
```

#### 3.1.1.2	Binary Arithmetic Instructions

**ADD指令**

0x00-0x03，0x05，0x80/0，0x81/0，0x83/0（group1的000）处，查询ADD指令具体细节，

![ADD1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADD1.jpg)

![ADD2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADD2.jpg)

![ADD3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADD3.jpg)

0x00处ADD指令格式为ADD r/m8 r8，译码函数选择`make_DHelper(G2E)`，执行函数选择`make_EHelper(add)`，操作数宽度为1，指令打包为`IDEXW(G2E, add, 1)`；0x01处ADD指令格式为ADD r/m16/r/m32 r16/r32，译码函数选择`make_DHelper(G2E)`，执行函数选择`make_EHelper(add)`，操作数宽度为默认的2/4，指令打包为`IDEX(G2E, add)`；0x02处ADD指令格式为ADD r8 r/m8，译码函数选择`make_DHelper(E2G)`，执行函数选择`make_EHelper(add)`，操作数宽度为1，指令打包为`IDEXW(E2G, add, 1)`；0x03处ADD指令格式为ADD r16/r32 r/m16/r/m32，译码函数选择`make_DHelper(E2G)`，执行函数选择`make_EHelper(add)`，操作数宽度为默认的2/4，指令打包为`IDEX(E2G, add)`；0x05处ADD指令格式为ADD AX/EAX imm16/32，译码函数选择`make_DHelper(I2a)`，执行函数选择`make_EHelper(add)`，操作数宽度为默认的2/4，指令打包为`IDEX(G2E, add)`；0x80/0（group1的000）处ADD指令格式为ADD r/m8 imm8，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(I2E)`，1，执行函数选择`make_EHelper(add)`，指令打包为`EX(add)`；0x81/0（group1的000）处ADD指令格式为ADD r/m16/r/m32 imm16/imm32，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(I2E)`，默认的2/4，执行函数选择`make_EHelper(add)`，指令打包为`EX(add)`；0x83/0（group1的000）处ADD指令格式为ADD r/m16/r/m32 imm8，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(SI2E)`，1，执行函数选择`make_EHelper(add)`，指令打包为`EX(add)`；在`opcode_table`数组对应位置填写打包好的ADD指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(add)`的声明，最后实现对应的执行函数，这里还需要注意执行函数中需要更新EFLAGS寄存器中的OF，SF，ZF，CF标志位。

```c
make_DHelper(G2E) {
  decode_op_rm(eip, id_dest, true, id_src, true);
}
make_DHelper(E2G) {
  decode_op_rm(eip, id_src, true, id_dest, true);
}
make_DHelper(I2a) {
  decode_op_a(eip, id_dest, true);
  decode_op_I(eip, id_src, true);
}
make_DHelper(I2E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
  decode_op_I(eip, id_src, true);
}
make_DHelper(SI2E) {
  assert(id_dest->width == 2 || id_dest->width == 4);
  decode_op_rm(eip, id_dest, true, NULL, false);
  id_src->width = 1;
  decode_op_SI(eip, id_src, true);
  if (id_dest->width == 2) {
    id_src->val &= 0xffff;
  }
}

make_EHelper(add) {
  rtl_add(&t2, &id_dest->val, &id_src->val); //rtl_add已经实现，此处直接调用
  operand_write(id_dest, &t2); //结果回写
  
  rtl_update_ZFSF(&t2, id_dest->width); //ZF，SF更新
  
  rtl_sltu(&t0, &t2, &id_dest->val);
  rtl_set_CF(&t0); //CF更新
  
  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_not(&t0);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0); //OF更新
  
  //printf("add 0x%08x to 0x%08x\n",id_src->val, id_dest->val);
  //rtl_get_CF(&t0);printf("CF: %d ,",t0);
  //rtl_get_OF(&t0);printf("OF: %d ,",t0);
  //rtl_get_ZF(&t0);printf("ZF: %d ,",t0);
  //rtl_get_SF(&t0);printf("SF: %d\n",t0);

  print_asm_template2(add);
}
```

**INC指令**

0x40-0x47，0xff/0（group5的000）处，查询INC指令具体细节，

![INC1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\INC1.jpg)

![INC2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\INC2.jpg)

![INC3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\INC3.jpg)

0x40-0x47处INC指令格式为INC r16/r32，译码函数选择`make_DHelper(r)`，执行函数选择`make_EHelper(inc)`，操作数宽度为默认的2/4，指令打包为`IDEX(r, inc)`；0xff/0（group5的000）处INC指令格式为INC r/m8/r/m16，译码函数和操作数宽度NEMU框架代码已经指定（只实现INC r/m16的形式）分别为`make_DHelper(E)`，2，执行函数选择`make_EHelper(inc)`，指令打包为`EX(inc)`；在`opcode_table`数组对应位置填写打包好的INC指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(inc)`的声明，最后实现对应的执行函数，这里还需要注意执行函数中需要更新EFLAGS寄存器中的OF，SF，ZF，CF标志位。

```c
make_DHelper(r) {
  decode_op_r(eip, id_dest, true);
}
make_DHelper(E) {
  decode_op_rm(eip, id_dest, true, NULL, false);
}

make_EHelper(inc) {
  id_src->val = 1;
  rtl_add(&t2, &id_dest->val, &id_src->val); //id_dest->val++
  operand_write(id_dest, &t2); //结果回写
  
  rtl_update_ZFSF(&t2, id_dest->width); //ZF，SF更新
  
  rtl_sltu(&t0, &t2, &id_dest->val);
  rtl_set_CF(&t0); //CF更新
  
  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_not(&t0);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0); //OF更新

  print_asm_template1(inc);
}
```

**SUB指令**

0x28-0x2b，0x2d，0x80/5，0x81/5，0x83/5（group1的101）处，查询SUB指令具体细节，

![SUB1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SUB1.jpg)

![SUB2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SUB2.jpg)

![SUB3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SUB3.jpg)

这里根据Opcode Map可知ADD，OR，SBB，AND，SUB，XOR，CMP指令对应位置的格式相同，所以在报告中就不重复说明译码函数和操作数宽度的选择，只需要参考ADD指令的实现部分即可，而与ADD指令中实现指令格式都不相同的会详细说明。

译码函数与操作数宽度选择参考ADD指令；执行函数`make_EHelper(sub)`在阶段一中已经实现，所以只需要在`opcode_table`数组对应位置填写打包好的SUB指令即可。

**DEC指令**

0x48-0x4f，0xfe/1（group4的001），0xff/1（group5的001）处，查询DEC指令具体细节，

![DEC1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\DEC1.jpg)

![DEC2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\DEC2.jpg)

![DEC3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\DEC3.jpg)

0x48-0x4f处DEC指令格式为DEC r16/r32，译码函数选择`make_DHelper(r)`，执行函数选择`make_EHelper(dec)`，操作数宽度为默认的2/4，指令打包为`IDEX(r, dec)`；0xfe/1（group4的001）处DEC指令格式为DEC r/m8，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(E)`，1，执行函数选择`make_EHelper(dec)`，指令打包为`EX(dec)`；0xff/1（group5的001）处DEC指令格式为DEC r/m16/r/m32，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(E)`，默认的2/4，执行函数选择`make_EHelper(dec)`，指令打包为`EX(dec)`；在`opcode_table`数组对应位置填写打包好的DEC指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(dec)`的声明，最后实现对应的执行函数，这里还需要注意执行函数中需要更新EFLAGS寄存器中的OF，SF，ZF，CF标志位。

```c
make_EHelper(dec) {
  id_src->val = 1;
  rtl_sub(&t2, &id_dest->val, &id_src->val); //id_dest->val--
  operand_write(id_dest, &t2); //结果回写

  rtl_update_ZFSF(&t2, id_dest->width); //ZF，SF更新

  rtl_sltu(&t0, &id_dest->val, &t2);
  rtl_set_CF(&t0); //CF更新

  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0); //OF更新

  print_asm_template1(dec);
}
```

**CMP指令**

0x38-0x3d，0x80/7，0x81/7，0x83/7（group1的111）处，查询CMP指令具体细节，

![CMP1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CMP1.jpg)

![CMP2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CMP2.jpg)

![CMP3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CMP3.jpg)

除0x3c处CMP指令外，其他CMP指令译码函数与操作数宽度选择参考ADD指令。

0x3c处CMP指令格式为CMP AX/EAX imm8，译码函数选择`make_DHelper(I2a)`，执行函数选择`make_EHelper(cmp)`，操作数宽度为1，指令打包为`IDEXW(G2E, cmp, 1)`；在`opcode_table`数组对应位置填写打包好的CMP指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(cmp)`的声明，最后实现对应的执行函数，这里还需要注意执行函数中需要更新EFLAGS寄存器中的OF，SF，ZF，CF标志位。

```C
make_EHelper(cmp) {
  rtl_sub(&t2, &id_dest->val, &id_src->val); //数值比较

  rtl_update_ZFSF(&t2, id_dest->width); //ZF，SF更新

  rtl_sltu(&t0, &id_dest->val, &t2);
  rtl_set_CF(&t0); //CF更新

  rtl_xor(&t0, &id_dest->val, &id_src->val);
  rtl_xor(&t1, &id_dest->val, &t2);
  rtl_and(&t0, &t0, &t1);
  rtl_msb(&t0, &t0, id_dest->width);
  rtl_set_OF(&t0); //OF更新
  
  //printf("cmp 0x%08x and 0x%08x\n",id_src->val, id_dest->val);
  //rtl_get_CF(&t0);printf("CF: %d ,",t0);
  //rtl_get_OF(&t0);printf("OF: %d ,",t0);
  //rtl_get_ZF(&t0);printf("ZF: %d ,",t0);
  //rtl_get_SF(&t0);printf("SF: %d\n",t0);
  
  print_asm_template2(cmp);
}
```

**NEG指令**

0xf6/3，0xf7/3（group3的011）处，查询NEG指令具体细节，

![NEG1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NEG1.jpg)

![NEG2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NEG2.jpg)

![NEG3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NEG3.jpg)

0xf6/3（group3的011）处NEG指令格式为NEG r/m8，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(E)`，1，执行函数选择`make_EHelper(neg)`，指令打包为`EX(neg)`；0xf7/3（group3的011）处INC指令格式为NEG r/m16/r/m32，译码函数和操作数宽度NEMU框架代码已经指定，分别为`make_DHelper(E)`，默认的2/4，执行函数选择`make_EHelper(neg)`，指令打包为`EX(neg)`；在`opcode_table`数组对应位置填写打包好的NEG指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(neg)`的声明，这里还需要实现需要使用到的`rtl_neq0`指令，

```c
static inline void rtl_neq0(rtlreg_t* dest, const rtlreg_t* src1) {
  // dest <- (src1 != 0 ? 1 : 0)
  *dest=(*src1!=0 ? 1 : 0);
}
```

最后实现对应的执行函数，这里还需要注意执行函数中需要更新EFLAGS寄存器中的OF，SF，ZF，CF标志位。

```c
make_EHelper(neg) {
  rtl_neq0(&t0,&id_dest->val); //id_dest->val是否等于0
  rtl_set_CF(&t0); //若等于0则置CF为0

  rtl_addi(&t1,&tzero,id_dest->val);
  rtl_not(&t1); //取反
  rtl_addi(&t1,&t1,1); //加1
  operand_write(id_dest,&t1); //结果回写
  
  rtl_update_ZFSF(&t1, id_dest->width); //ZF，SF更新
  
  rtl_xor(&t2,&t1,&id_dest->val);
  rtl_not(&t2);
  rtl_msb(&t2,&t2,id_dest->width);
  rtl_set_OF(&t2); //OF更新

  print_asm_template1(neg);
}
```

**ADC指令**

0x10-0x13处，查询ADC指令具体细节，

![ADC1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADC1.jpg)

![ADC2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADC2.jpg)

![ADC3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ADC3.jpg)

译码函数与操作数宽度选择参考ADD指令；执行函数`make_EHelper(adc)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(adc)`的声明并在`opcode_table`数组对应位置填写打包好的ADC指令即可。

**SBB指令**

0x18-0x1b，0x80/3，0x81/3，0x83/3（group1的011）处，查询SBB指令具体细节，

![SBB1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SBB1.jpg)

![SBB2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SBB2.jpg)

![SBB3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SBB3.jpg)

译码函数与操作数宽度选择参考ADD指令；执行函数`make_EHelper(sbb)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(sbb)`的声明并在`opcode_table`数组对应位置填写打包好的SBB指令即可。

**MUL指令**

0xf6/4，0xf7/4（group3的100）处，查询MUL指令具体细节，

![MUL1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MUL1.jpg)

![MUL2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MUL2.jpg)

![MUL3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\MUL3.jpg)

这里根据Opcode Map可知group3中TEST，NOT，NEG，MUL，IMUL，DIV，IDIV指令对应位置的格式相同，所以在报告中就不重复说明译码函数和操作数宽度的选择，只需要参考NEG指令的实现部分即可，而与NEG指令中实现指令格式都不相同（例如：不在group3内的上述指令）的会详细说明。

译码函数与操作数宽度选择参考NEG指令；执行函数`make_EHelper(imul)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(mul)`的声明并在`opcode_table`数组对应位置填写打包好的MUL指令即可。

**IMUL指令**

0xf6/5，0xf7/5（group3的101），Two-Byte Opcode Map0xaf处，查询IMUL指令具体细节，

![IMUL1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IMUL1.jpg)

![IMUL2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IMUL2.jpg)

![IMUL3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IMUL3.jpg)

![IMUL4](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IMUL4.jpg)

除0xaf处IMUL指令外，其他IMUL指令译码函数与操作数宽度选择参考ADD指令。其中0xf6/5，0xf7/5处为单运算数操作，执行函数选择`make_EHelper(imul1)`，指令打包为`EX(imul1)`。

0xaf处IMUL指令格式为IMUL r16/r32 r/m16/r/m32，译码函数选择`make_DHelper(E2G)`，操作数宽度为默认的2/4，为双运算数操作，执行函数选择`make_EHelper(imul2)`，指令打包为`IDEX(E2G, imul2)`；执行函数`make_EHelper(imul1)`和`make_EHelper(imul2)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(imul1)`和`make_EHelper(imul2)`的声明并在`opcode_table`数组对应位置填写打包好的MUL指令即可。

**DIV指令**

0xf6/6，0xf7/6（group3的110）处，查询DIV指令具体细节，

![DIV1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\DIV1.jpg)

![DIV2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\DIV2.jpg)

译码函数与操作数宽度选择参考NEG指令；执行函数`make_EHelper(div)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(div)`的声明并在`opcode_table`数组对应位置填写打包好的DIV指令即可。

**IDIV指令**

0xf6/7，0xf7/7（group3的111）处，查询IDIV指令具体细节，

![DIV1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IDIV1.jpg)

![DIV2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IDIV2.jpg)

译码函数与操作数宽度选择参考NEG指令；执行函数`make_EHelper(idiv)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(idiv)`的声明并在`opcode_table`数组对应位置填写打包好的IDIV指令即可。

#### 3.1.1.3	Logical Instructions

**NOT指令**

0xf6/7，0xf7/7（group3的111）处，查询NOT指令具体细节，

![DIV1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NOT1.jpg)

![DIV2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NOT2.jpg)

译码函数与操作数宽度选择参考NEG指令；执行函数选择`make_EHelper(not)`，指令打包为`EX(not)`；在`opcode_table`数组对应位置填写打包好的NOT指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(not)`的声明，这里还需要实现需要使用到的`rtl_not`指令，

```c
static inline void rtl_not(rtlreg_t* dest) {
  // dest <- ~dest
  *dest=~(*dest);
}
```

最后实现对应的执行函数。

```c
make_EHelper(not) {
  rtl_not(&id_dest->val); //rtl_not指令已经实现，此处直接调用
  operand_write(id_dest, &id_dest->val); //结果回写

  print_asm_template1(not);
}
```

**AND指令**

0x20-0x23，0x25，0x80/4，0x81/4，0x83/4（group1的100）处，查询AND指令具体细节，

![AND1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\AND1.jpg)

![AND2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\AND2.jpg)

![AND3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\AND3.jpg)

译码函数与操作数宽度选择参考ADD指令；执行函数选择`make_EHelper(and)`，在`opcode_table`数组对应位置填写打包好的AND指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(and)`的声明，最后实现对应的执行函数。

```c
make_EHelper(and) {
  rtl_and(&t0, &id_dest->val, &id_src->val); //rtl_and指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_set_CF(&tzero); //CF置为0
  rtl_set_OF(&tzero); //OF置为0
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新

  print_asm_template2(and);
}
```

**OR指令**

0x08-0x0b，0x0d，0x80/1，0x81/1，0x83/1（group1的001）处，查询OR指令具体细节，

![AND1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR1.jpg)

![AND2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR2.jpg)

![AND3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR3.jpg)

译码函数与操作数宽度选择参考ADD指令；执行函数选择`make_EHelper(or)`，在`opcode_table`数组对应位置填写打包好的OR指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(or)`的声明，最后实现对应的执行函数。

```C
make_EHelper(or) {
  rtl_or(&t0, &id_dest->val, &id_src->val); //rtl_or指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_set_CF(&tzero); //CF置为0
  rtl_set_OF(&tzero); //OF置为0
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新

  print_asm_template2(or);
}
```

**XOR指令**

0x30-0x33，0x35，0x80/6，0x81/6，0x83/6（group1的110）处，查询OR指令具体细节，

![AND1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR1.jpg)

![AND2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR2.jpg)

![AND3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OR3.jpg)

译码函数与操作数宽度选择参考ADD指令；执行函数选择`make_EHelper(xor)`，在`opcode_table`数组对应位置填写打包好的XOR指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(xor)`的声明，最后实现对应的执行函数。

```c
make_EHelper(xor) {
  rtl_xor(&t0, &id_dest->val, &id_src->val); //rtl_xor指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_set_CF(&tzero); //CF置为0
  rtl_set_OF(&tzero); //OF置为0
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新

  print_asm_template2(xor);
}
```

**SAL(SHL)指令**

0xc0/4，0xc1/4，0xd0/4，0xd1/4，0xd2/4，0xd3/4（group2的100）处，查询SAL(SHL)指令具体细节，

![S1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\S1.jpg)

![S2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\S2.jpg)

![S3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\S3.jpg)

![S4](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\S4.jpg)

0xc0/4（group2的110）处SHL指令格式为SHL r/m8 imm8，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_Ib2E)`，1，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；0xc1/4（group2的110）处SHL指令格式为SHL r/m16/r/m32 imm8，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_Ib2E)`，默认的2/4，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；0xd0/4（group2的110）处SHL指令格式为SHL r/m8 1，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_1_E)`，1，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；0xd1/4（group2的110）处SHL指令格式为SHL r/m16/r/m32 1，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_1_E)`，默认的2/4，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；0xd2/4（group2的110）处SHL指令格式为SHL r/m8 CL，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_cl2E)`，1，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；0xd3/4（group2的110）处SHL指令格式为SHL r/m16/r/m32 CL，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(gp2_cl2E)`，默认的2/4，执行函数选择`make_EHelper(shl)`，指令打包为`EX(shl)`；在`opcode_table`数组对应位置填写打包好的SHL指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(shl)`的声明，最后实现对应的执行函数。

```c
make_EHelper(shl) {
  rtl_shl(&t0, &id_dest->val, &id_src->val); //rtl_shl指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shl);
}
```

**SHR指令**

0xc0/5，0xc1/5，0xd0/5，0xd1/5，0xd2/5，0xd3/5（group2的101）处，SHR指令具体细节可查看SAL(SHL)指令部分。

根据Opcode Map和SAL(SHL)指令细节可知，ROL，SAL(SHL)，SHR，SAR指令对应位置的格式相同，所以在报告中就不重复说明译码函数和操作数宽度的选择，只需要参考SAL(SHL)指令的实现部分即可，区别在于执行函数的选择机和实现。

译码函数与操作数宽度选择参考SAL(SHL)指令；执行函数选择`make_EHelper(shr)`，指令打包为`EX(shr)`；在`opcode_table`数组对应位置填写打包好的SHR指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(shr)`的声明，最后实现对应的执行函数。

```c
make_EHelper(shr) {
  rtl_shr(&t0, &id_dest->val, &id_src->val); //rtl_shr指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(shr);
}
```

**SAR指令**

0xc0/7，0xc1/7，0xd0/7，0xd1/7，0xd2/7，0xd3/7（group2的111）处，SAR指令具体细节可查看SAL(SHL)指令部分。

译码函数与操作数宽度选择参考SAL(SHL)指令；执行函数选择`make_EHelper(sar)`，指令打包为`EX(sar)`；在`opcode_table`数组对应位置填写打包好的SAR指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(sar)`的声明，最后实现对应的执行函数。

```c
make_EHelper(sar) {
  rtl_sar(&t0, &id_dest->val, &id_src->val); //rtl_sar指令已经实现，此处直接调用
  operand_write(id_dest, &t0); //结果回写
  
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新
  
  // unnecessary to update CF and OF in NEMU

  print_asm_template2(sar);
}
```

**ROL指令**

0xc0/0，0xc1/0，0xd0/0，0xd1/0，0xd2/0，0xd3/0（group2的000）处，查询ROL指令具体细节，

![ROL1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ROL1.jpg)

![ROL2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ROL2.jpg)

![ROL3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ROL3.jpg)

![ROL4](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\ROL4.jpg)

译码函数与操作数宽度选择参考SAL(SHL)指令；执行函数选择`make_EHelper(rol)`，指令打包为`EX(rol)`；在`opcode_table`数组对应位置填写打包好的ROL指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(rol)`的声明，最后实现对应的执行函数。

```c
make_EHelper(rol) {
  t0=id_dest->width*8-id_src->val;
  rtl_shr(&t1,&id_dest->val,&t0); //逻辑右移id_dest->width*8-id_src->val位
  rtl_shl(&t2,&id_dest->val,&id_src->val); //逻辑左移id_src->val位
  rtl_or(&t3,&t1,&t2); //逻辑右移左移结果按位或
  operand_write(id_dest,&t3); //结果回写
  
  // unnecessary to update CF and OF in NEMU
  
  print_asm_template2(rol);
}
```

**SETcc指令**

Two-Byte Opcode Map0x0f 90-0x0f 9f处，是双字节操作码，查询SETcc指令具体细节，

![SETCC1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SETCC1.jpg)

![SETCC2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\SETCC2.jpg)

NEMU中将所有SETcc指令打包为一个，所以所有SETcc类的指令在NEMU中格式都为SETcc r/m8，译码函数选择`make_DHelper(E)`，操作数宽度为1，执行函数选择`make_EHelper(setcc)`，指令打包为`IDEXW(E, setcc, 1)`，在`opcode_table`数组对应位置填写打包好的SETcc指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(setcc)`的声明，这里还需要实现需要使用到的`rtl_setcc`指令（函数位置：`nemu/src/cpu/exec/exec.c`）具体实现可参考NEMU框架代码中的注释和i386手册中SETcc指令细节，

```c
void rtl_setcc(rtlreg_t* dest, uint8_t subcode) {
  bool invert = subcode & 0x1;
  enum {
    CC_O, CC_NO, CC_B,  CC_NB,
    CC_E, CC_NE, CC_BE, CC_NBE,
    CC_S, CC_NS, CC_P,  CC_NP,
    CC_L, CC_NL, CC_LE, CC_NLE
  };

  // TODO: Query EFLAGS to determine whether the condition code is satisfied.
  // dest <- ( cc is satisfied ? 1 : 0)
  switch (subcode & 0xe) {
    case CC_O:
      *dest=(eflags_OF == 1);
      break;
    case CC_B:
      *dest=(eflags_CF == 1);
      break;
    case CC_E:
      *dest=(eflags_ZF == 1);
      break;
    case CC_BE:
      *dest=((eflags_CF == 1) || (eflags_ZF == 1));
      break;
    case CC_S:
      *dest=(eflags_SF == 1);
      break;
    case CC_L:
      *dest=(eflags_SF != eflags_OF);
      break;
    case CC_LE:
      *dest=(eflags_ZF == 1 || (eflags_SF != eflags_OF));
      break;
    case CC_P: panic("nemu does not have PF");
    default: panic("error in rtl_setcc");
    
  }

  if (invert) {
    rtl_xori(dest, dest, 0x1);
  }
}
```

最后实现对应的执行函数。

```c
make_EHelper(setcc) {
  uint8_t subcode = decoding.opcode & 0xf; //判断SETcc指令具体格式
  rtl_setcc(&t2, subcode); //rtl_setcc指令已经实现，此处直接调用
  operand_write(id_dest, &t2); //结果回写

  print_asm("set%s %s", get_cc_name(subcode), id_dest->str);
}
```

**TEST指令**

0x84，0x85，0xa8，0xf6/0，0xf7/0（group3的000）处，查询TEST指令具体细节，

![TEST1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\TEST1.jpg)

![TEST2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\TEST2.jpg)

![TEST3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\TEST3.jpg)

0x84处TEST指令格式为TEST r/m8 r8，译码函数选择`make_DHelper(G2E)`，操作数宽度为1，执行函数选择`make_EHelper(test)`，指令打包为`IDEXW(G2E, test, 1)`；0x85处TEST指令格式为TEST r/m16/r/m32 r16/r32，译码函数选择`make_DHelper(G2E)`，操作数宽度为默认的2/4，执行函数选择`make_EHelper(test)`，指令打包为`IDEX(G2E, test)`；0xa8处TEST指令格式为TEST AL imm8，译码函数选择`make_DHelper(I2a)`，操作数宽度为1，执行函数选择`make_EHelper(test)`，指令打包为`IDEXW(I2a, test, 1)`；0xf6/0（group3的000）处TEST指令格式为TEST r/m8 imm8，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(E)`，1，但`make_DHelper(E)`只能读出指令中的r/m信息，立即数的读取仍需要一个译码函数来实现，这里选择`make_DHelper(test_I)`，执行函数选择`make_EHelper(test)`，指令打包为`IDEX(test_I, test)`；0xf7/0（group3的000）处TEST指令格式为TEST r/m16/r/m32 imm16/imm32，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(E)`，默认的2/4，立即数的读取通过`make_DHelper(test_I)`译码函数来实现，执行函数选择`make_EHelper(test)`，指令打包为`IDEX(test_I, test)`；在`opcode_table`数组对应位置填写打包好的TEST指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(test)`的声明，最后实现对应的执行函数。

```c
make_DHelper(test_I) {
  decode_op_I(eip, id_src, true);
}

make_EHelper(test) {
  rtl_and(&t0, &id_dest->val, &id_src->val); //rtl_and指令已经实现，此处直接调用
  
  rtl_set_CF(&tzero); //CF置为0
  rtl_set_OF(&tzero); //OF置为0
  rtl_update_ZFSF(&t0, id_dest->width); //ZF，SF更新

  print_asm_template2(test);
}
```

#### 3.1.1.4	Control Transfer Instructions

**JMP指令**

0xe9，0xeb，0xff/4（group5的100）处，查询JMP指令具体细节，

![JMP1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JMP1.jpg)

![JMP2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JMP2.jpg)

![JMP3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JMP3.jpg)

0xe9处JMP指令格式为JMP rel16/rel32，译码函数选择`make_DHelper(J)`，操作数宽度为默认的2/4，执行函数选择`make_EHelper(jmp)`，指令打包为`IDEX(J, jmp)`；0xeb处JMP指令格式为JMP rel8，译码函数选择`make_DHelper(J)`，操作数宽度为1，执行函数选择`make_EHelper(jmp)`，指令打包为`IDEXW(J, jmp, 1)`；0xff/4（group5的100）处JMP指令格式为JMP r/m16/r/m32，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(E)`，默认的2/4，执行函数选择`make_EHelper(jmp_rm)`，指令打包为`EX(jmp_rm)`；这两个执行函数NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(jmp)`和`make_EHelper(jmp_rm)`的声明并在`opcode_table`数组对应位置填写打包好的JMP指令即可。

**Jcc指令**

0x70-0x7f，Two-Byte Opcode Map0x0f 80-0x0f 8f处，查询Jcc指令具体细节，

![JCC1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JCC1.jpg)

![JCC2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JCC2.jpg)

![JCC3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\JCC3.jpg)

NEMU中将所有Jcc指令打包为一个，0x70-0x7f处Jcc指令格式为Jcc rel8，译码函数选择`make_DHelper(J)`，操作数宽度为1，执行函数选择`make_EHelper(jcc)`，指令打包为`IDEXW(J, jcc, 1)`，Two-Byte Opcode Map0x0f 80-0x0f 8f处Jcc指令格式为Jcc rel16/32，译码函数选择`make_DHelper(J)`，操作数宽度为默认的2/4，执行函数选择`make_EHelper(jcc)`，指令打包为`IDEX(J, jcc)`，执行函数`make_EHelper(jcc)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(jcc)`的声明并在`opcode_table`数组对应位置填写打包好的Jcc指令即可。

**CALL指令**

0xe8，0xff/2（group5的010）处，查询CALL指令具体细节，

![CALL1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CALL1.jpg)

![CALL2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CALL2.jpg)

![CALL3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\CALL3.jpg)

0xe8处CALL指令在阶段一已经实现，0xff/2（group5的010）处CALL指令格式为CALL r/m16/r/m32，译码函数和操作数宽度NEMU框架代码中已经指定，分别为`make_DHelper(E)`，默认的2/4，执行函数选择`make_EHelper(call_rm)`，指令打包为`EX(call_rm)`，执行函数`make_EHelper(call_rm)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(call_rm)`的声明并在`opcode_table`数组对应位置填写打包好的CALL指令即可。

**RET指令**

0xc3处RET指令在阶段一已经实现。

#### 3.1.1.5	Miscellaneous Instructions

**LEA指令**

0x8d处，查询LEA指令具体细节，

![LEA1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\LEA1.jpg)

![LEA2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\LEA2.jpg)

0x8d处LEA指令格式LEA r16/r32 m，译码函数选择`make_DHelper(lea_M2G)`，操作数宽度为默认的2/4，执行函数选择`make_EHelper(lea)`，指令打包为`IDEX(lea_M2G, lea)`，执行函数`make_EHelper(lea)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(lea)`的声明并在`opcode_table`数组对应位置填写打包好的LEA指令即可。

**NOP指令**

0x90处，查询NOP指令具体细节，

![NOP](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\NOP.jpg)

0x90处NOP指令即空指令，不需要译码函数，也无操作数，执行函数选择`make_EHelper(nop)`，指令打包为`EX(nop)`，执行函数`make_EHelper(nop)`NEMU框架代码中已经实现，所以只需要到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(nop)`的声明并在`opcode_table`数组对应位置填写打包好的NOP指令即可。

**XCHG指令**

PA2XCHG指令即为NOP指令，已经实现。

### 3.1.2	实现Differential Testing

NEMU的框架代码已经实现NEMU与QEMU执行相同程序前的初始化工作，包括

- 调用`init_difftest()`函数（在`nemu/src/monitor/diff-test/diff-test.c`中定义）来在后台启动QEMU
- 在`load_img()`的最后将客户程序拷贝一份副本到QEMU模拟的内存中
- 在`restart()`中调用`init_qemu_reg()`函数（在`nemu/src/monitor/diff-test/diff-test.c`中定义），来把QEMU的通用寄存器设置成和NEMU一样

所以实现Differential Testing在`difftest_step()`函数(在`nemu/src/monitor/diff-test/diff-test.c`中定义)中完成寄存器内容的比较和在寄存器内容不一致的情况下中断程序执行并打印报错信息即可，

```c
if (r.eip != cpu.eip)
  {
    diff = true;
    printf("eip different!\nqemu eip:0x%08x, nemu eip:0x%08x\n", r.eip, cpu.eip);
  }
  if (r.eax != cpu.eax)
  {
    diff = true;
    printf("eax different!\nqemu eax:0x%08x, nemu eax:0x%08x eip:0x%08x\n", r.eax, cpu.eax, cpu.eip);
  }
  if (r.ecx != cpu.ecx)
  {
    diff = true;
    printf("ecx different!\nqemu ecx:0x%08x, nemu ecx:0x%08x eip:0x%08x\n", r.ecx, cpu.ecx, cpu.eip);
  }
  if (r.edx != cpu.edx)
  {
    diff = true;
    printf("edx different!\nqemu edx:0x%08x, nemu edx:0x%08x eip:0x%08x\n", r.edx, cpu.edx, cpu.eip);
  }
  if (r.ebx != cpu.ebx)
  {
    diff = true;
    printf("ebx different!\nqemu ebx:0x%08x, nemu ebx:0x%08x eip:0x%08x\n", r.ebx, cpu.ebx, cpu.eip);
  }
  if (r.esp != cpu.esp)
  {
    diff = true;
    printf("esp different!\nqemu esp:0x%08x, nemu esp:0x%08x eip:0x%08x\n", r.esp, cpu.esp, cpu.eip);
  }
  if (r.ebp != cpu.ebp)
  {
    diff = true;
    printf("ebp different!\nqemu ebp:0x%08x, nemu ebp:0x%08x eip:0x%08x\n", r.ebp, cpu.ebp, cpu.eip);
  }
  if (r.esi != cpu.esi)
  {
    diff = true;
    printf("esi different!\nqemu esi:0x%08x, nemu esi:0x%08x eip:0x%08x\n", r.esi, cpu.esi, cpu.eip);
  }
  if (r.edi != cpu.edi)
  {
    diff = true;
    printf("edi different!\nqemu edi:0x%08x, nemu edi:0x%08x eip:0x%08x\n", r.edi, cpu.edi, cpu.eip);
  }
```

实现了此功能后在`nemu/include/common.h`中定义宏DIFF_TEST即可启动Differential Testing。

## 3.2	运行结果

成功在NEMU上运行`nexus-am/tests/cputest`目录下所有`cputest`程序。

![cputest](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\cputest.jpg)

成功实现Differential Testing。

![Differential Testing](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\Differential Testing.jpg)

## 3.3	Bug总结

阶段二写出的Bug很多，译码函数的选择，操作数宽度的选择，指令填写位置错误（单字节操作码填写到双字节操作码中），执行函数实现有错误等都可能导致程序NEMU无法正常执行指令，译码函数的选择一方面要多查看i386手册，一方面要注意NEMU框架代码中所给的译码函数及注释，部分译码函数已经在注释中写明了用在那些指令的译码过程；操作数宽度的选择需要多查看i386手册；执行函数的实现尽量按照i386手册中各指令的具体操作来实现，同时也要注意NEMU框架代码中的注释提示。

Differential Testing是在PA2非常好用的调试器，可以大大减少锁定出错指令的时间，在Debug时运用PA1实现的简易调试器并参考Differential Testing的报错信息可以大大提升Debug效率，应该尽早实现并利用Differential Testing来进行调试。

# 4	阶段三

到目前为止NEMU已经可以成功运行了cputest中的所有测试用例，但这些测试用例都只能默默地进行纯粹的计算，为了能让NEMU可以运行更多的程序，在阶段三需要在AM中添加IOE的功能，来让NEMU运行可与外界进行交互的程序。需要IOE功能包括串口，时钟，键盘和VGA共四种。

在NEMU中加入IOE需要在`nemu/include/common.h`中定义宏HAS_IOE，并在AM中实现相应的API为程序提供IOE的抽象（在`nexus-am/am/arch/x86-nemu/src/ioe.c`中定义）：

- `unsigned long _uptime()`用于返回系统启动后经过的毫秒数
- `int _read_key()`用于返回按键的键盘码，若无按键，则返回`_KEY_NONE`
- `_Screen _screen`结构用于指示屏幕的大小
- `void _draw_rect(const uint32_t *pixels, int x, int y, int w, int h)`用于将`pixels`指定的矩形像素绘制到屏幕中以(x, y)和(x+w, y+h)两点连线为对角线的矩形区域
- `void _draw_sync()`用于将之前的绘制内容同步到屏幕上（在NEMU中绘制内容总是会同步到屏幕上，因而无需实现此API）
- `void _ioe_init()`用于进行IOE相关的初始化工作，调用后程序才能正确使用上述IOE相关的API

## 4.1	实现方法及代码分析

NEMU框架代码中已经提供了设备的代码（`nemu/src/device`目录下），代码提供了以下模块的模拟：

- 端口映射I/O和内存映射I/O两种I/O编址方式
- 串口，时钟，键盘，VGA四种设备（不可编程）
- 对端口I/O的模拟（`nemu/src/device/io/port-io.c`）
- 和SDL库相关的代码（`nemu/src/device/device.c`），NEMU使用SDL库来模拟计算机的标准输入输出

但程序并不能直接运行在NEMU上，所以还需要在AM中实现相应的API来提供IOE的抽象来支撑需要IOE的程序的运行。串口的实现需要在NEMU中实现端口映射I/O；时钟的实现需要补全AM中IOE的API中的`_uptime()`函数；键盘的实现需要补全AM中IOE的API中的`_read_key()`函数；VGA的实现需要在NEMU中加入对内存映射I/O的判断，并补全AM中IOE的API中的`_draw_rect()`函数。

### 4.1.1	串口

`nemu/src/device/serial.c`模拟了串口的功能，具有数据寄存器和状态寄存器，串口初始化时会注册0x3F8处长度为8个字节的端口作为其寄存器。NEMU串行模拟计算机系统的工作，串口的状态寄存器可以一直处于空闲状态，每当CPU往数据寄存器中写入数据时。串口会将数据传送到主机的标准输出。AM中也已经提供了串口的功能（`nexus-am/am/arch/x86-nemu/src/trm.c`文件中），为了让程序使用串口进行输出，需要在NEMU中实现端口映射I/O。

**in指令**

0xec，0xed处，查询IN指令具体细节，

![IN1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IN1.jpg)

![IN2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\IN2.jpg)

0xec处IN指令格式IN AL DX，译码函数选择`make_DHelper(in_dx2a)`，操作数宽度为1，执行函数选择`make_EHelper(in)`，指令打包为`IDEXW(in_dx2a, in, 1)`；0xed处IN指令格式IN AX/EAX DX，译码函数选择`make_DHelper(in_dx2a)`，操作数宽度为默认的2/4，执行函数选择`make_EHelper(in)`，指令打包为`IDEX(in_dx2a, in)`，在`opcode_table`数组对应位置填写打包好的IN指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(in)`的声明，最后实现对应的执行函数。

```c
make_DHelper(in_dx2a) {
  id_src->type = OP_TYPE_REG;
  id_src->reg = R_DX;
  rtl_lr_w(&id_src->val, R_DX);
#ifdef DEBUG
  sprintf(id_src->str, "(%%dx)");
#endif

  decode_op_a(eip, id_dest, false);
}

uint32_t pio_read(ioaddr_t, int);

make_EHelper(in) {
  t0=pio_read(id_src->val,id_src->width);
  //pio_read()NEMU框架代码中已经实现，此处直接调用，实现从端口DX读取数据
  operand_write(id_dest,&t0); //输入结果写入

  print_asm_template2(in);

#ifdef DIFF_TEST
  diff_test_skip_qemu();
#endif
}
```

**out指令**

0xee处，查询OUT指令具体细节，

![OUT1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OUT1.jpg)

![OUT2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\OUT2.jpg)

0xee处OUT指令格式OUT DX AL，译码函数选择`make_DHelper(out_a2dx)`，操作数宽度为1，执行函数选择`make_EHelper(out)`，指令打包为`IDEXW(out_a2dx, out, 1)`，在`opcode_table`数组对应位置填写打包好的OUT指令，再到`nemu/src/cpu/exec/all-instr.h`文件中添加执行函数`make_EHelper(out)`的声明，最后实现对应的执行函数。

```c
make_DHelper(out_a2dx) {
  decode_op_a(eip, id_src, true);

  id_dest->type = OP_TYPE_REG;
  id_dest->reg = R_DX;
  rtl_lr_w(&id_dest->val, R_DX);
#ifdef DEBUG
  sprintf(id_dest->str, "(%%dx)");
#endif
}

void pio_write(ioaddr_t, int, uint32_t);

make_EHelper(out) {
  pio_write(id_dest->val,id_src->width,id_src->val);
  //pio_write()NEMU框架代码中已经实现，此处直接调用，实现从将数据写入到端口DX
  print_asm_template2(out);

#ifdef DIFF_TEST
  diff_test_skip_qemu();
#endif
}
```

实现完成后还需要在`nexus-am/am/arch/x86-nemu/src/trm.c`中定义宏HAS_SERIAL，开启NEMU的串口。

### 4.1.2	时钟

nemu/src/device/timer.c模拟了i8253计时器的功能，初始化时将会注册0x48处的端口作为RTC寄存器，CPU 可以通过I/O指令访问这一寄存器，获得当前时间（单位是ms）。为了实现时钟需要补全AM中时钟的API的`unsigned long _uptime()`函数，用于返回NEMU启动后经过的毫秒数。

```c
void _ioe_init() { //IOE初始化
  boot_time = inl(RTC_PORT); //IOE初始化时间
}

unsigned long _uptime() {
  return inl(RTC_PORT)-boot_time; //当前时间需要减去IOE初始化时间才是NEMU启动后经过的时间
}
```

### 4.1.3	键盘

`nemu/src/device/keyboard.c`模拟i8042通用设备接口芯片的功能，其大部分功能也被简化，只保留了键盘接口，i8042初始化时会注册0x60处的端口作为数据寄存器，注册0x64处的端口作为状态寄存器。键盘的工作方式为当按下一个键的时候，键盘将会发送该键的通码（make code），当释放一个键的时候，键盘将会发送该键的断码（break code），所以每当用户敲下/释放按键时，i8042将会把相应的键盘码放入数据寄存器，同时把状态寄存器的标志设置为1，表示有按键事件发生。CPU可以通过端口I/O访问这些寄存器，获得键盘码。为了实现键盘需要补全AM中键盘的API的`int _read_key()`函数，用于返回按键的键盘码。

```c
int _read_key() {
  uint32_t impress_key = inb(0x64) & 0x1; //0x64状态寄存器检测
  if(impress_key)
    return inl(0x60); //若状态寄存器为1，则返回0x60数据寄存器内容（键盘码）
  else
    return _KEY_NONE; //否则返回_KEY_NONE
}
```

### 4.1.4	VGA

VGA 可以用于显示颜色像素，`nemu/src/device/vga.c`模拟了VGA的功能，VGA初始化时注册了从0x40000开始的一段用于映射到video memory的物理内存。在NEMU中，video memory是唯一使用内
存映射I/O方式访问的I/O空间。NEMU中只模拟了400x300x32的图形模式，一个像素占32 个bit的存储空间，R(red)，G(green)，B(blue)，A(alpha)各占8bit，其中VGA不使用alpha的信息。为了实现VGA，需要先在NEMU中添加内存映射I/O（目录：`nemu/src/memory/memory.c`），注意添加头文件`divide/mmio.h`

```c
uint32_t paddr_read(paddr_t addr, int len) {
  //通过is_mmio()函数判断一个物理地址是否被映射到I/O空间
  //如果是，is_mmio()会返回映射号，否则返回-1
  int port = is_mmio(addr);
  if(port != -1)
    return mmio_read(addr, len, port); //若被映射到I/O空间，则调用mmio_read()读取
  else
    return pmem_rw(addr, uint32_t) & (~0u >> ((4 - len) << 3)); //否则读取内存
}

void paddr_write(paddr_t addr, int len, uint32_t data) {
  int port = is_mmio(addr); //判断写入地址是否被映射到I/O空间
  if(port != -1)
    mmio_write(addr, len, data, port); //若被映射到I/O空间，则调用mmio_write()写入
  else
    memcpy(guest_to_host(addr), &data, len); //否则写入内存
}
```

再补全AM中VGA的API的`void _draw_rect(const uint32_t *pixels, int x, int y, int w, int h)`函数，用于将`pixels`指定的矩形像素绘制到屏幕中以(x, y)和(x+w, y+h)两点连线为对角线的矩形区域。

```c
void _draw_rect(const uint32_t *pixels, int x, int y, int w, int h) {
  /*
  int i;
  for (i = 0; i < _screen.width * _screen.height; i++) {
    fb[i] = i;
  }
  */
  int len = sizeof(uint32_t);
  for(int i = 0; i < h; i++) //逐行遍历
  {
    //绘制`pixels`指定的矩形像素
    memcpy(fb + (y + i) * _screen.width + x, pixels + i * w, len * w);
  }
}
```

## 4.2	运行结果

实现串口，成功运行`nexus-am/apps`目录下`hello`程序。

![hello](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\hello.jpg)

实现时钟，成功运行`nexus-am/tests`目录下`timetest`程序。

![timetest](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\timetest.jpg)

成功运行`nexus-am/apps`目录下跑分程序Dhrystone，Coremark和microbench。

![dhrystone](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\dhrystone.jpg)

![coremark](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\coremark.jpg)

![microbench](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\microbench.jpg)

实现键盘，成功运行`nexus-am/tests`目录下`keytest`程序。

![keytest](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\keytest.jpg)

实现VGA，成功运行`nexus-am/tests`目录下`videotest`程序。

![videotest](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\videotest.jpg)

成功运行`nexus-am/apps`目录下`typing`打字小游戏程序。

![typing](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\typing.jpg)

成功运行`nexus-am/litenes`目录下红白机模拟器。

![mario](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\mario.jpg)

## 4.3	Bug总结

阶段三需要实现的功能虽然比较多，但代码量较少，关键在于理解IOE实现的关键机制，具体函数的实现按照PA实验流程中的提示并参考AM下`native`中的实现来实现即可，最开始的VGA实现中采取了逐像素绘制的方法，导致绘制不正确，参考了`native`的实现后做了修正。实现后可通过与`native`下运行的结果进行比对来比较是否正确。

# 5	手册必答题

1. 在`nemu/include/cpu/rtl.h`中，你会看到由`static inline`开头定义的各种RTL指令函数。选择其中一个函数，分别尝试去掉`static`，去掉`inline`或去掉两者，然后重新进行编译，你会看到发生错误。请分别解释为什么会发生这些错误？你有办法证明你的想法吗？

   `inline`的作用仅仅是建议编译器做内联开展处理，而不是强制。内联函数（`inline`）把函数代码在编译时直接拷贝到程序中，这样就不用执行时另外读取函数代码，可以减少CPU的系统开销，并且程序的整体速度将加快。

   加入`static`，这样内部调用函数时会内联，而外部调用该函数时则不会内联。在调用这种函数的时候，gcc会在其调用处将其汇编码展开编译而不为这个函数生成独立的汇编码。除了以下几种情况外：

   1. 函数的地址被使用的时候。如通过函数指针对函数进行了间接调用。这种情况下就不得不为`static inline`函数生成独立的汇编码，否则它没有自己的地址。
   2. 其他一些无法展开的情况，比如函数本身有递归调用自身的行为等。

   尝试去掉`static`（`rtl_push`指令，之后运行`dummy`）：

   ![error1](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\error1.jpg)

   程序运行失败，报错信息如上，在非`static`的`inline`函数`rtl_push()`中调用了`static inline`函数，导致`rtl_push`指令无法正常运行。

   但如果去掉`static`的RTL指令中并未调用其他RTL指令，则程序可以正常运行。

   尝试去掉`inline`（`rtl_push`指令，之后运行`dummy`）：

   ![error2](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\error2.jpg)

   程序运行失败，警告信息如上，可以通过将将makefile文件中的编译选项-Wall –Werror去掉，来使程序正常编译。`static`函数限制了适用范围，只能在当前源文件中使用，多个文件链接时，`static`函数只是文件域内可见，因此不会发生重定义的错误。

   尝试去掉`static inline`（`rtl_push`指令，之后运行`dummy`）：

   ![error3](C:\Users\Lenovo\Desktop\学习\PA实验报告\PA2picture\error3.jpg)

   程序运行失败，报错信息如上，出现了`rtl_push()`函数重定义的问题，这是因为每个`.c`文件的编译都是独立的，该`.c`文件用到的外部函数都在编译时预留一个符号，只有等到所有可重定位的目标文件生成后，链接时才给这些符号地址。对于NEMU中每个调用了`rtl_push()`函数的.c文件，来说，都包含了`rtl_push()`的声明和实现，所以在链接时就会发生报错。

2. 了解Makefile，请描述你在nemu目录下敲入make后，make程序如何组织`.c`和`.h`文件，最终生成可执行文件`nemu/build/nemu`。（这个问题包括两个方面：Makefile的工作方式和编译链接的过程。）关于Makefile工作方式的提示：

   - Makefile中使用了变量，包含文件等特性
   - Makefile运用并重写了一些implicit rules
   - 在man make中搜索-n选项，也许会对你有帮助
   - RTFM

   Makefile的工作方式：

   1. 读入所有的Makefile
   2. 读入被include的其它Makefile
   3. 初始化文件中的变量
   4. 推导隐晦规则，并分析所有规则
   5. 为所有的目标文件创建依赖关系链
   6. 根据依赖关系，决定哪些目标要重新生成
   7. 执行生成命令

   Makefile里主要包含了五个东西：显式规则、隐晦规则、变量定义、文件指示和注释。

   1. 显式规则：显式规则说明了，如何生成一个或多的的目标文件。这是由Makefile的书写者明显指出，要生成的文件，文件的依赖文件，生成的命令。

   2. 隐晦规则：由于make有自动推导的功能，所以隐晦的规则可以让开发者比较粗糙地简略地书写Makefile，这是由make所支持的。

   3. 变量的定义：在Makefile中我们要定义一系列的变量，变量一般都是字符串，当Makefile被执行时，其中的变量都会被扩展到相应的引用位置上。

   4. 文件指示：其包括了三个部分，一个是在一个Makefile中引用另一个Makefile，就像C语言中的include一样；另一个是指根据某些情况指定Makefile中的有效部分，就像C语言中的预编译#if一样；还有就是定义一个多行的命令。有关这一部分的内容，我会在后续的部分中讲述。

   5. 注释：Makefile中只有行注释，和UNIX的Shell脚本一样，其注释是用“#”字符。如果要在的Makefile中使用“#”字符，可以用反斜框进行转义，如：“\\#”。

      此外在Makefile中的命令，必须要以[Tab]键开始。

   Makefile的编译链接的过程：

   1. 读入所有Makefile
   2. 读入被include的其他文件
      这里主要为定义了git_commit(msg)函数的Makefile.git文件。
   3. 初始化文件中的变量
      如：各文件的路径，最终目标文件名称NAME=nemu，CC、CFLAGS 等隐含变量的修改，源文件.c与中间文件.o的设置。
   4. 推导隐晦规则并分析所有规则
      app依赖于BINATY（nemu可执行文件）；BINARY依赖于OBJS（build 文件夹下的所有.o文件）；build 的各.o文件依赖于src文件夹下的所有.c文件；各.c文件依赖于其中定义的.h文件（隐含规则）。
   5. 为所有目标文件创造关系链
   6. 根据依赖文件决定哪些目标要重新生成
   7. 执行生成命令
      Makefile 的.DEFAULT_GOAL = app语句声明默认的维护目标app。故键入make后程序根据4中所提及的依赖关系进行实现：首先，将所有.c文件编译生成所有.o 中间文件，随后，将各.o文件链接生成nemu可执行文件。

# 6	感悟与体会

1. PA2中最费时间的部分在于NEMU执行指令的框架代码的理解，宏定义函数，执行执行的解耦合，同类指令打包等都是先前很少遇到的代码写法，在理解这一部分就花费了很长的时间，几乎接近实现所有指令时间的一半，最开始的译码函数选择，操作数宽度选择，指令打包等都基本属于试一下这样子对不对的状态，不过随着更多指令的实现，对这部分框架代码的理解也逐渐加深，后面指令的实现速度就快了很多，出现的Bug也比较少了。
2. 高效的调试器可以大大提升Debug的效率，可惜我并没有在PA2一开始就实现Differential Testing，而是在MOVSX指令实现的过程中遇到了Bug才实现的，如果在指令开始前就实现并使用Differential Testing，实验所需的时间应该还可以缩短。
3. 尽量找连续的时间来做需要大段时间的工作，比如写PA和实验报告，每次从头理清头绪和现在的进度并进入状态需要一定的时间，将工作分段到不同的时间也尽量按照工作内容来进行，尽量不要将连续的工作分到两个时间段。
4. 不要高估自己的执行力，即使只剩几行的代码也尽量一并完成，因为下次打开PA就有可能是在一周之后了，期间有可能被其他的ddl占用了时间，或者只是单纯的拖延。