# SPDZ 激活函数秘密共享计算协议

本文给出在秘密共享域中计算 MLP 激活函数的协议化描述，适用于 Falcon + MP-SPDZ 场景。

## 1. 记号与参与方

- 参与方集合：$\mathcal{P}=\{P_0,\dots,P_{m-1}\}$。
- 对任意明文标量 $x$，其加法秘密共享记为 $[ x ]=([ x ]_0,\dots,[ x ]_{m-1})$，满足

$$
\sum_{k=0}^{m-1}[ x]_k = x
$$

- 对矩阵/向量逐元素同样记为 $[ \mathbf{X} ]$。
- 固定点表示：$x\in\mathbb{R}$ 编码为 $\tilde{x}=\lfloor x\cdot 2^f\rfloor$，其中 $f$ 为小数位精度（例如 $f=16$）。

## 2. 协议目标

给定每层线性输出秘密共享 $[ \mathbf{Z}^{(l)} ]\in\mathbb{R}^{B\times d_l}$，安全计算：

$$
[ \mathbf{A}^{(l)} ] = [ \sigma_l(\mathbf{Z}^{(l)}) ],
\quad
[ \mathbf{D}^{(l)} ] = \left[ \frac{\partial \sigma_l}{\partial \mathbf{Z}^{(l)}} \right]
$$

其中 $B$ 为 batch size，$d_l$ 为该层输出维度。

## 3. 基础安全算子

以下算子均在秘密共享域执行：

- $\Pi_{\text{Add}}$: 安全加法（本地逐份额相加）。
- $\Pi_{\text{Mul}}$: 安全乘法（Beaver triple）。
- $\Pi_{\text{LT}}$: 安全比较，输出秘密比特。
- $\Pi_{\text{IfElse}}$: 安全条件选择，$[ b]\in\{0,1\}$ 时

$$
\Pi_{\text{IfElse}}([ b],[ x],[ y])
= [ b]\cdot[ x] + (1-[ b])\cdot[ y]
$$

- $\Pi_{\exp}$: 安全指数。
- $\Pi_{\text{Inv}}$: 安全倒数。

注：$\Pi_{\exp}$、$\Pi_{\text{Inv}}$ 在 MP-SPDZ 中通常由固定点近似（多项式/迭代）实现。

## 4. 协议总览

记单个元素输入为 $[ z]$。协议对每个元素并行执行，得到 $[ a],[ d]$。

### 4.1 ReLU（采用 SPDZ 标准方法）

ReLU 定义：

$$
\operatorname{ReLU}(z)=\max(0,z)
$$

标准 SPDZ 做法（不 reveal，不明文分支）：

1. 安全比较得到秘密比特

$$
[ b] \leftarrow \Pi_{\text{LT}}(0,[ z])
\quad\text{其中 } b=\mathbf{1}[z>0]
$$

2. 安全选择得到激活值

$$
[ a] \leftarrow \Pi_{\text{IfElse}}([ b],[ z],0)
=[ b]\cdot[ z]
$$

3. 导数（按 $z=0$ 取 0 的子梯度）

$$
[ d] \leftarrow [ b]
$$

因此：

$$
d =
\begin{cases}
1, & z>0\\
0, & z\le 0
\end{cases}
$$

### 4.2 Logistic / Sigmoid

Sigmoid 定义：

$$
\sigma(z)=\frac{1}{1+e^{-z}}
$$

协议步骤：

1. 取负

$$
[ u]\leftarrow -[ z]
$$

2. 安全指数

$$
[ e]\leftarrow \Pi_{\exp}([ u])
$$

3. 分母

$$
[ s]\leftarrow 1+[ e]
$$

4. 倒数得到激活

$$
[ a]\leftarrow \Pi_{\text{Inv}}([ s])
$$

5. 导数

$$
[ d]\leftarrow \Pi_{\text{Mul}}([ a],1-[ a])
$$

即：

$$
d=\sigma(z)(1-\sigma(z))
$$

### 4.3 Identity

$$
a=z,\quad d=1
$$

协议中直接赋值：

$$
[ a]\leftarrow[ z],
\quad
[ d]\leftarrow 1
$$

### 4.4 Softmax（向量输入）

对单个样本的 logits 向量 $[ \mathbf{z} ]=([ z_1],\dots,[ z_C])$：

1. 稳定化（可选但推荐）

$$
[ m]\leftarrow \Pi_{\max}([ z_1],\dots,[ z_C])
$$

$$
[ t_j]\leftarrow [ z_j]-[ m]
$$

2. 指数与归一化

$$
[ e_j]\leftarrow \Pi_{\exp}([ t_j]),
\quad
[ s]\leftarrow\sum_{j=1}^{C}[ e_j],
\quad
[ a_j]\leftarrow \Pi_{\text{Mul}}([ e_j],\Pi_{\text{Inv}}([ s]))
$$

3. 导数（完整 Jacobian）

$$
\left[ \frac{\partial a_j}{\partial z_k} \right]
= \left[ a_j(\delta_{jk}-a_k) \right]
$$

若框架只传递逐元素导数近似，可用

$$
[ d_j]\approx[ a_j(1-a_j)]
$$

但这不是完整 Jacobian。

## 5. 统一接口协议

定义统一激活协议 $\Pi_{\text{SPDZ-Act}}$：

**输入**：

- $[ \mathbf{Z} ]\in\mathbb{R}^{B\times d}$
- 激活类型 $\texttt{act\_id}\in\{\texttt{LOGISTIC},\texttt{RELU},\texttt{IDENTITY},\texttt{SOFTMAX}\}$
- 计算模式 $\texttt{mode}\in\{\texttt{ACTIVATION},\texttt{ACTIVATION\_FAST}\}$

**输出**：

- 若 $\texttt{mode}=\texttt{ACTIVATION}$：输出 $[ \mathbf{A} ],[ \mathbf{D} ]$
- 若 $\texttt{mode}=\texttt{ACTIVATION\_FAST}$：仅输出 $[ \mathbf{A} ]$

**步骤**：

1. 各方将本地份额按元素提交给 SPDZ 在线阶段。
2. SPDZ 依据 $\texttt{act\_id}$ 逐元素或逐向量调用对应子协议。
3. 输出结果重新分享给各方（每方仅得到结果份额）。

## 6. 正确性与安全性说明

### 6.1 正确性

基于加法秘密共享的线性同态与 Beaver 乘法正确性，协议输出满足：

$$
\sum_{k=0}^{m-1}[ a]_k = \sigma(z),
\quad
\sum_{k=0}^{m-1}[ d]_k = \sigma'(z)
$$

### 6.2 安全性（半诚实模型）

- 单个参与方仅见随机份额，无法恢复明文输入/输出。
- ReLU 使用 $\Pi_{\text{LT}}+\Pi_{\text{IfElse}}$ 标准做法，不应使用 reveal 判断符号。
- 泄露面主要来自协议元数据（batch 大小、层宽、激活类型等公共参数），不包含样本值本身。

## 7. 复杂度简述

设总元素数 $N=B\cdot d$：

- ReLU：$N$ 次比较 + $N$ 次乘法（或等价选择门）。
- Sigmoid：$N$ 次指数 + $N$ 次倒数 + $N$ 次乘法。
- Identity：线性开销。
- Softmax：每样本 $C$ 次指数 + 1 次倒数 + $C$ 次乘法，另含可选安全 max。

## 8. 实施约束（与工程实现对齐）

- 固定点精度需在执行器与 MP-SPDZ 程序中一致（例如 $f=16$）。
- 输出导数格式需与反向传播接口一致：
  - 二分类 sigmoid/relu/identity 可直接逐元素导数。
  - 多分类 softmax 若与交叉熵合并实现，可直接输出 $\hat{y}-y$ 避免显式 Jacobian。

---

本协议文本可直接作为 Falcon 中 SPDZ 激活模块的规范说明。若需要严格隐私语义，ReLU 必须采用本文第 4.1 节的标准 SPDZ 方法。