# Falcon VFL-MLP 隐私保护训练完整流程

## 一、符号约定

| 符号 | 含义 |
|------|------|
| $x$ | 明文（plaintext）标量或矩阵 |
| $[x]$ | PHE（门限 Paillier）加密密文，$[x] = \text{Enc}_{pk}(x)$ |
| $\langle x \rangle$ | 加法秘密共享，party $P_k$ 持有份额 $\langle x \rangle_k$，满足 $\sum_k \langle x \rangle_k = x$ |
| $[x] \oplus [y]$ | 同态加法 $= [x+y]$ |
| $c \odot [x]$ | 明文标量与密文数乘 $= [c \cdot x]$ |
| $\bigoplus_k$ | 多项同态聚合加 |
| $\odot_{\text{EW}}$ | 逐元素乘（element-wise multiply）|
| $\tilde{x}$ | 经裁剪后的值 |
| $\hat{x}$ | 经裁剪并加噪后的值 |
| $C$ | 逐样本 L2 裁剪阈值（clipping threshold，公开超参数）|
| $\sigma_\text{dp}$ | 差分隐私噪声标准差（公开超参数）|
| $\mathbf{N} \sim \mathcal{N}(0,\sigma^2\mathbf{I})$ | 各方联合注入的高斯噪声矩阵 |

---

## 二、系统设定

- **参与方**：$m$ 个 party：主动方 $P_0$（持有标签 $\mathbf{y}$），被动方 $P_1, \ldots, P_{m-1}$
- **特征划分（VFL）**：$P_k$ 持有本地特征 $\mathbf{X}^{(k)} \in \mathbb{R}^{n \times d_k}$，总特征维度 $d = \sum_k d_k$
- **MLP 结构**：共 $L$ 个权重层（第 $0$ 至第 $L-1$ 层），各层描述如下：

$$\text{层} \; l \; (0 \leq l \leq L-1):\quad \mathbf{W}^{(l)} \in \mathbb{R}^{n_l \times n_{l+1}}, \; \mathbf{b}^{(l)} \in \mathbb{R}^{n_{l+1}}, \; \text{激活函数} \; \sigma_l$$

其中 $n_0 = d$（输入维度），$n_L$（输出维度）。

- **第 0 层权重的横向拆分**：因各方持有不同特征，层 0 的权重矩阵按特征行分块，$P_k$ 持有 $\mathbf{W}^{(0,k)} \in \mathbb{R}^{d_k \times n_1}$（对应其 $d_k$ 个特征的那些行），即：

$$\mathbf{W}^{(0)} = \begin{bmatrix} \mathbf{W}^{(0,0)} \\ \mathbf{W}^{(0,1)} \\ \vdots \\ \mathbf{W}^{(0,m-1)} \end{bmatrix} \in \mathbb{R}^{d \times n_1}$$

- **加密体制**：门限 Paillier 半同态加密（Threshold PHE），$P_0$ 负责加密，各方联合解密（collaborative decryption）

---

## 三、核心子协议

### $\Pi_\text{C2S}$：密文 → 秘密共享（Cipher-to-SecretShare）

**输入**：$P_0$ 掌握密文向量 $[z_1], \ldots, [z_n]$，已广播至所有方。  
**功能**：将 $[z_i]$ 分解为加法份额 $\langle z_i \rangle_0, \ldots, \langle z_i \rangle_{m-1}$。

**步骤**（对每个 $[z]$ 执行）：

$$\text{(1)} \quad \forall k \neq 0: \; P_k \text{ 采样随机数 } r_k \xleftarrow{R} \mathbb{Z},\; \text{计算 } [r_k] \leftarrow \text{Enc}(r_k),\; \text{发送 } [r_k] \rightarrow P_0$$

$$\text{(2)} \quad P_0 \text{ 计算残差密文：} [\tilde{z}] = [z] \oplus \bigoplus_{k=1}^{m-1}\!\big((-1) \odot [r_k]\big) = \left[z - \sum_{k=1}^{m-1} r_k\right]$$

$$\text{(3)} \quad \text{各方联合门限解密：} \tilde{z} \leftarrow \text{CollabDec}([\tilde{z}])$$

$$\text{(4)} \quad P_0 \text{ 持有 } \langle z \rangle_0 = \tilde{z};\quad P_k \; (k \neq 0) \text{ 持有 } \langle z \rangle_k = r_k$$

**验证**：$\langle z \rangle_0 + \sum_{k=1}^{m-1} \langle z \rangle_k = \tilde{z} + \sum_{k} r_k = z$ ✓

---

### $\Pi_\text{S2C}$：秘密共享 → 密文（SecretShare-to-Cipher）

**输入**：各方分别持有份额 $\langle z \rangle_k$。  
**功能**：组合还原密文 $[z] = \left[\sum_k \langle z \rangle_k\right]$。

$$\text{(1)} \quad \forall k: \; P_k \text{ 加密 } [\langle z \rangle_k] \leftarrow \text{Enc}(\langle z \rangle_k),\; \text{发送给 } P_0$$

$$\text{(2)} \quad P_0 \text{ 同态聚合：} [z] = \bigoplus_{k=0}^{m-1} [\langle z \rangle_k]$$

$$\text{(3)} \quad P_0 \text{ 广播 } [z] \text{ 至所有方}$$

---

### $\Pi_\text{SCM}$：秘密共享 × 密文矩阵乘（ShareCipherMatMul）

**输入**：各方持有矩阵份额 $\langle \mathbf{A} \rangle_k \in \mathbb{R}^{p \times q}$，加密矩阵 $[\mathbf{B}] \in \mathbb{R}^{q \times r}$（已广播）。  
**功能**：联合计算 $[\mathbf{A} \cdot \mathbf{B}]$，其中 $\mathbf{A} = \sum_k \langle \mathbf{A} \rangle_k$。

$$\text{(1)} \quad \forall k: \; P_k \text{ 本地计算 } [\widetilde{\mathbf{C}}_k] = \langle \mathbf{A} \rangle_k \odot [\mathbf{B}] \in \mathbb{R}^{p \times r}$$

$$\quad\quad \left([{\widetilde{C}_k}]_{ij} = \sum_{l=1}^{q} \langle a_{il} \rangle_k \odot [b_{lj}]\right)$$

$$\text{(2)} \quad P_k \; (k \neq 0) \text{ 发送 } [\widetilde{\mathbf{C}}_k] \rightarrow P_0;\quad P_0 \text{ 聚合：} [\mathbf{C}] = \bigoplus_{k=0}^{m-1} [\widetilde{\mathbf{C}}_k] = [\mathbf{A} \cdot \mathbf{B}]$$

$$\text{(3)} \quad P_0 \text{ 广播 } [\mathbf{C}] \text{ 至所有方}$$

---

### $\text{SPDZ-Activation}$：MPC 激活函数计算

**输入**：各方持有份额 $\langle \mathbf{Z} \rangle_k$，激活函数 $\sigma$。  
**功能**：通过 SPDZ 协议计算激活值及其导数的秘密共享。

$$\langle \mathbf{A} \rangle, \; \langle \mathbf{A}' \rangle \leftarrow \text{SPDZ}\!\left(\langle \mathbf{Z} \rangle, \; \sigma\right), \quad \mathbf{A} = \sigma(\mathbf{Z}), \quad \mathbf{A}' = \sigma'(\mathbf{Z})$$

各方将各自份额发送至 SPDZ 引擎，返回 $\langle \sigma(\mathbf{Z}) \rangle$ 和 $\langle \sigma'(\mathbf{Z}) \rangle$ 的份额。

---

### $\Pi_\text{DPClip}$：隐私保护裁剪与噪声注入（DP Clip & Noise）

**输入**：PHE 密文矩阵 $[\mathbf{Z}^{(k)}] \in \mathbb{R}^{B \times n_1}$，裁剪阈值 $C$，噪声标准差 $\sigma_\text{dp}$，各方数量 $m$。  
**功能**：对各方局部贡献逐样本 L2 裁剪，并注入高斯差分隐私噪声，返回密文 $[\hat{\mathbf{Z}}^{(k)}]$。  
**执行方**：全部 $m$ 个 party 共同参与（C2S、SPDZ、S2C 均为多方协议）。

**步骤一：密文转秘密共享**

$$\langle \mathbf{Z}^{(k)} \rangle \leftarrow \Pi_\text{C2S}\!\left([\mathbf{Z}^{(k)}]\right)$$

此后各方 $P_j$ 均持有份额 $\langle \mathbf{Z}^{(k)} \rangle_j \in \mathbb{R}^{B \times n_1}$。

**步骤二：SPDZ 逐样本 L2 裁剪**

对批次中每个样本 $i \in [B]$，独立执行以下 SPDZ 电路（所有运算均在秘密共享域进行）：

**(2a) 计算逐样本 L2 平方范数**：

$$\langle q_i^{(k)} \rangle = \sum_{j=1}^{n_1} \langle z_{ij}^{(k)} \rangle^2 = \big\|\langle \mathbf{z}_i^{(k)} \rangle\big\|_2^2$$

（各元素平方通过 Beaver 乘法三元组在 SPDZ 内安全计算）

**(2b) 通过 Newton-Raphson 迭代计算倒数平方根**：

初始化 $\langle r_i^{(0)} \rangle = \text{const}$（取决于动态范围），迭代 $T_\text{NR}$ 步（通常 2–3 步）：

$$\langle r_i^{(t+1)} \rangle = \frac{1}{2}\langle r_i^{(t)} \rangle \cdot \left(3 - \langle q_i^{(k)} \rangle \cdot \langle r_i^{(t)} \rangle^2\right)$$

最终得到 $\langle r_i^{(k)} \rangle \approx 1/\sqrt{\langle q_i^{(k)} \rangle + \varepsilon}$（加小量 $\varepsilon > 0$ 防止零除）。

**(2c) 计算裁剪比例因子并钳制**：

$$\langle \nu_i^{(k)} \rangle = C \cdot \langle r_i^{(k)} \rangle = \frac{C}{\|\mathbf{z}_i^{(k)}\|_2}$$

$$\langle s_i^{(k)} \rangle = \min\!\left(1,\; \langle \nu_i^{(k)} \rangle\right)$$

（$\min$ 操作通过 SPDZ 安全比较 + 条件选择实现，无泄露）

**(2d) 逐元素缩放获得裁剪后秘密共享**：

$$\langle \tilde{z}_{ij}^{(k)} \rangle = \langle s_i^{(k)} \rangle \cdot \langle z_{ij}^{(k)} \rangle, \quad \forall j \in [n_1]$$

SPDZ 引擎将裁剪结果份额 $\langle \tilde{\mathbf{Z}}^{(k)} \rangle$ 返回至各方。

**步骤三：各方本地注入高斯噪声（明文操作，无需额外通信）**

各方 $P_j$ 在本地独立采样噪声矩阵：

$$\mathbf{N}_j^{(k)} \sim \mathcal{N}\!\left(0,\; \frac{\sigma_\text{dp}^2}{m} \cdot \mathbf{I}_{B \times n_1}\right)$$

直接叠加至本地份额：

$$\langle \hat{z}_{ij}^{(k)} \rangle_j \leftarrow \langle \tilde{z}_{ij}^{(k)} \rangle_j + [\mathbf{N}_j^{(k)}]_{ij}, \quad \forall i,j$$

由高斯分布可加性，各方份额求和后等价于注入单一完整噪声：

$$\hat{\mathbf{Z}}^{(k)} = \tilde{\mathbf{Z}}^{(k)} + \sum_{j=0}^{m-1} \mathbf{N}_j^{(k)} = \tilde{\mathbf{Z}}^{(k)} + \mathbf{N}^{(k)}, \quad \mathbf{N}^{(k)} \sim \mathcal{N}\!\left(0,\; \sigma_\text{dp}^2 \cdot \mathbf{I}\right)$$

**步骤四：秘密共享转密文**

$$[\hat{\mathbf{Z}}^{(k)}] \leftarrow \Pi_\text{S2C}\!\left(\langle \hat{\mathbf{Z}}^{(k)} \rangle\right)$$

**输出**：$[\hat{\mathbf{Z}}^{(k)}]$，可直接参与后续 $P_0$ 的同态聚合。

---

## 四、初始化阶段

**由 $P_0$ 执行**，对所有层 $l = 0, 1, \ldots, L-1$：

**Xavier 均匀初始化**（明文采样）：

$$\mathbf{W}^{(l)} \sim \mathcal{U}\!\left[-\sqrt{\tfrac{6}{n_l + n_{l+1}}},\; \sqrt{\tfrac{6}{n_l + n_{l+1}}}\right]^{n_l \times n_{l+1}}, \quad \mathbf{b}^{(l)} \sim \mathcal{U}[\ldots]$$

**加密并广播**：

$$[\mathbf{W}^{(l)}] \leftarrow \text{Enc}\!\left(\mathbf{W}^{(l)}\right), \quad [\mathbf{b}^{(l)}] \leftarrow \text{Enc}\!\left(\mathbf{b}^{(l)}\right)$$

$$P_0 \xrightarrow{\{[\mathbf{W}^{(l)}],\; [\mathbf{b}^{(l)}]\}_{l=0}^{L-1}} P_1, P_2, \ldots, P_{m-1}$$

此后，所有方均持有相同的加密权重 $\{[\mathbf{W}^{(l)}], [\mathbf{b}^{(l)}]\}$。

---

## 五、单次迭代训练（mini-batch $\mathcal{B}$，$B = |\mathcal{B}|$）

### 5.1 批次同步

$P_0$ 随机选取批次索引 $\mathcal{B} \subset [n]$，广播至所有方。各方提取本地批次样本：

$$\mathbf{X}_\mathcal{B}^{(k)} \in \mathbb{R}^{B \times d_k} \quad (k = 0, 1, \ldots, m-1)$$

---

### 5.2 前向传播（Forward Computation）

#### 层 0（第一个权重层，跨方计算，含 DP 裁剪与噪声注入）

因各方只持有部分特征，每方 $P_k$ 本地计算其特征对应的局部聚合（PHE 明文-密文矩阵乘）：

$$[\mathbf{Z}_\mathcal{B}^{(0,k)}] = \mathbf{X}_\mathcal{B}^{(k)} \cdot [\mathbf{W}^{(0,k)}] \in \mathbb{R}^{B \times n_1}, \quad \left([Z^{(0,k)}_{\mathcal{B},ij}] = \sum_{f=1}^{d_k} x^{(k)}_{\mathcal{B},if} \odot [W^{(0,k)}_{fj}]\right)$$

**【新增】各方执行 DP 裁剪与噪声注入（聚合前）**

在上传给 $P_0$ 前，对每一方的局部贡献逐样本执行 $\Pi_\text{DPClip}$（所有方联合参与，共 $m$ 轮）：

$$[\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}] \leftarrow \Pi_\text{DPClip}\!\left([\mathbf{Z}_\mathcal{B}^{(0,k)}],\; C,\; \sigma_\text{dp}\right), \quad k = 0, 1, \ldots, m-1$$

等价于在明文域对每个样本独立执行：

$$\hat{\mathbf{z}}_{\mathcal{B},i}^{(0,k)} = \underbrace{\min\!\left(1,\; \frac{C}{\|\mathbf{z}_{\mathcal{B},i}^{(0,k)}\|_2}\right)}_{\text{裁剪因子 }s_i^{(k)}} \cdot \mathbf{z}_{\mathcal{B},i}^{(0,k)} + \mathbf{n}_i^{(k)}, \quad \mathbf{n}_i^{(k)} \sim \mathcal{N}(0, \sigma_\text{dp}^2 \mathbf{I}_{n_1}), \quad \forall i \in [B]$$

> **$\Pi_\text{DPClip}$ 执行顺序**（对第 $k$ 方，全部 $m$ 方共同参与）：
> 1. $\Pi_\text{C2S}$：$[\mathbf{Z}_\mathcal{B}^{(0,k)}] \rightarrow \langle \mathbf{Z}_\mathcal{B}^{(0,k)} \rangle$
> 2. SPDZ 逐样本裁剪：$\langle \tilde{\mathbf{Z}}_\mathcal{B}^{(0,k)} \rangle \leftarrow \text{SPDZ-Clip}(\langle \mathbf{Z}_\mathcal{B}^{(0,k)} \rangle, C)$，内含平方 → Newton 迭代 $1/\sqrt{q}$ → 比较钳制 → 缩放
> 3. 各方本地加噪：$\langle \hat{\mathbf{Z}}_\mathcal{B}^{(0,k)} \rangle_j \leftarrow \langle \tilde{\mathbf{Z}}_\mathcal{B}^{(0,k)} \rangle_j + \mathbf{N}_j^{(k)}$，$\mathbf{N}_j^{(k)} \sim \mathcal{N}(0, \frac{\sigma_\text{dp}^2}{m}\mathbf{I})$（本地明文操作）
> 4. $\Pi_\text{S2C}$：$\langle \hat{\mathbf{Z}}_\mathcal{B}^{(0,k)} \rangle \rightarrow [\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}]$

被动方 $P_k$ ($k \neq 0$) 序列化并上传**裁剪加噪后**的密文至 $P_0$：

$$P_k \xrightarrow{[\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}]} P_0$$

主动方 $P_0$ 聚合所有方的裁剪加噪贡献，同态加入偏置：

$$[\hat{\mathbf{Z}}_\mathcal{B}^{(0)}] = \bigoplus_{k=0}^{m-1} [\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}] \oplus \mathbf{1}_B \otimes [\mathbf{b}^{(0)}] \in \mathbb{R}^{B \times n_1}$$

$P_0$ 广播 $[\hat{\mathbf{Z}}_\mathcal{B}^{(0)}]$，然后执行密文转秘密共享：

$$\langle \hat{\mathbf{Z}}_\mathcal{B}^{(0)} \rangle \leftarrow \Pi_\text{C2S}\!\left([\hat{\mathbf{Z}}_\mathcal{B}^{(0)}]\right)$$

通过 SPDZ 联合计算激活函数及其导数的秘密共享：

$$\langle \mathbf{A}_\mathcal{B}^{(0)} \rangle, \; \langle (\mathbf{A}_\mathcal{B}^{(0)})' \rangle \leftarrow \text{SPDZ-Activation}\!\left(\langle \hat{\mathbf{Z}}_\mathcal{B}^{(0)} \rangle,\; \sigma_0\right)$$

$$\text{其中 } \mathbf{A}_\mathcal{B}^{(0)} = \sigma_0\!\left(\hat{\mathbf{Z}}_\mathcal{B}^{(0)}\right) \in \mathbb{R}^{B \times n_1}, \quad (\mathbf{A}_\mathcal{B}^{(0)})' = \sigma_0'\!\left(\hat{\mathbf{Z}}_\mathcal{B}^{(0)}\right) \in \mathbb{R}^{B \times n_1}$$

---

#### 层 $l$（$1 \leq l \leq L-1$，隐藏层与输出层）

输入：前层激活秘密共享 $\langle \mathbf{A}_\mathcal{B}^{(l-1)} \rangle$，加密权重 $[\mathbf{W}^{(l)}]$，$[\mathbf{b}^{(l)}]$

通过 $\Pi_\text{SCM}$ 计算预激活值（秘密共享份额 × 密文权重矩阵）：

$$[\mathbf{Z}_\mathcal{B}^{(l)}] = \Pi_\text{SCM}\!\left(\langle \mathbf{A}_\mathcal{B}^{(l-1)} \rangle,\; [\mathbf{W}^{(l)}]\right) \oplus \mathbf{1}_B \otimes [\mathbf{b}^{(l)}]$$

$$= \left[\mathbf{A}_\mathcal{B}^{(l-1)} \cdot \mathbf{W}^{(l)} + \mathbf{1}_B \mathbf{b}^{(l)\top}\right] \in \mathbb{R}^{B \times n_{l+1}}$$

密文转秘密共享：

$$\langle \mathbf{Z}_\mathcal{B}^{(l)} \rangle \leftarrow \Pi_\text{C2S}\!\left([\mathbf{Z}_\mathcal{B}^{(l)}]\right)$$

SPDZ 激活：

$$\langle \mathbf{A}_\mathcal{B}^{(l)} \rangle, \; \langle (\mathbf{A}_\mathcal{B}^{(l)})' \rangle \leftarrow \text{SPDZ-Activation}\!\left(\langle \mathbf{Z}_\mathcal{B}^{(l)} \rangle,\; \sigma_l\right)$$

---

#### 输出层（$l = L-1$）特殊处理

最后一层的激活值即预测值，将秘密共享转回密文（供损失计算使用）：

$$[\hat{\mathbf{Y}}_\mathcal{B}] \leftarrow \Pi_\text{S2C}\!\left(\langle \mathbf{A}_\mathcal{B}^{(L-1)} \rangle\right) \in \mathbb{R}^{B \times n_L}$$

---

### 5.3 反向传播（Backward Computation）

**记号说明**（与前向对应）：
- $\boldsymbol{\Delta}^{(l)} \in \mathbb{R}^{B \times n_{l+1}}$：层 $l$ 的加密误差项（delta）
- $\nabla_{\mathbf{W}^{(l)}} \in \mathbb{R}^{n_l \times n_{l+1}}$：层 $l$ 权重梯度（加密形式）
- $\nabla_{\mathbf{b}^{(l)}} \in \mathbb{R}^{n_{l+1}}$：层 $l$ 偏置梯度（加密形式）

---

#### 步骤 1：输出层 delta（$l = L-1$）

$P_0$（持有真实标签）构造加密标签，计算预测误差：

$$[\mathbf{y}_\mathcal{B}] \leftarrow \text{Enc}(\mathbf{y}_\mathcal{B})$$

$$[\boldsymbol{\Delta}^{(L-1)}] = [\hat{\mathbf{Y}}_\mathcal{B}] \oplus (-1) \odot [\mathbf{y}_\mathcal{B}] = [\hat{\mathbf{Y}}_\mathcal{B} - \mathbf{y}_\mathcal{B}] \in \mathbb{R}^{B \times n_L}$$

（仅当输出激活与损失函数相匹配时此形式成立，如 sigmoid + 交叉熵、softmax + 交叉熵、线性 + MSE）

$P_0$ 广播 $[\boldsymbol{\Delta}^{(L-1)}]$ 至所有方。

---

#### 步骤 2：最后一层权重梯度（层 $L-1$）

输入激活为前层秘密共享 $\langle \mathbf{A}_\mathcal{B}^{(L-2)} \rangle$，delta 为密文 $[\boldsymbol{\Delta}^{(L-1)}]$，通过 $\Pi_\text{SCM}$ 计算：

$$[\nabla_{\mathbf{W}^{(L-1)}}] = -\frac{\eta}{B} \cdot \Pi_\text{SCM}\!\left(\langle \mathbf{A}_\mathcal{B}^{(L-2)} \rangle^\top,\; [\boldsymbol{\Delta}^{(L-1)}]\right) \in \mathbb{R}^{n_{L-1} \times n_L}$$

偏置梯度（仅 $P_0$ 执行，再广播）：

$$[\nabla_{\mathbf{b}^{(L-1)}}] = -\frac{\eta}{B} \cdot \bigoplus_{i=1}^{B} [\boldsymbol{\Delta}_i^{(L-1)}] \in \mathbb{R}^{n_L}$$

若启用 L2 正则化（正则系数 $\alpha$），追加正则化梯度（仅 $P_0$ 执行，再广播）：

$$[\nabla_{\mathbf{W}^{(L-1)}}] \leftarrow [\nabla_{\mathbf{W}^{(L-1)}}] \oplus \left(-\frac{\eta \alpha}{B}\right) \odot [\mathbf{W}^{(L-1)}]$$

---

#### 步骤 3：逐层向前回传（$l$ 从 $L-1$ 降至 $1$）

**3a. 更新层 $l-1$ 的 delta**

将当前层 delta 转为秘密共享：

$$\langle \boldsymbol{\Delta}^{(l)} \rangle \leftarrow \Pi_\text{C2S}\!\left([\boldsymbol{\Delta}^{(l)}]\right)$$

通过 $\Pi_\text{SCM}$ 反向传播穿过权重矩阵：

$$[\mathbf{D}^{(l-1)}] = \Pi_\text{SCM}\!\left(\langle \boldsymbol{\Delta}^{(l)} \rangle,\; [\mathbf{W}^{(l)}]^\top\right) = [\boldsymbol{\Delta}^{(l)} \cdot (\mathbf{W}^{(l)})^\top] \in \mathbb{R}^{B \times n_l}$$

逐元素乘以前层激活函数的导数秘密共享（each party 用本地份额乘以 $[\mathbf{D}^{(l-1)}]$，再聚合）：

$$[\boldsymbol{\Delta}^{(l-1)}] = [\mathbf{D}^{(l-1)}] \odot_\text{EW} \langle (\mathbf{A}_\mathcal{B}^{(l-1)})' \rangle$$

具体执行：

$$\forall k:\; P_k \text{ 本地计算 } [\widetilde{\mathbf{D}}_k] = \langle (\mathbf{A}_\mathcal{B}^{(l-1)})' \rangle_k \odot_\text{EW} [\mathbf{D}^{(l-1)}]$$

$$P_0 \text{ 聚合并广播：} [\boldsymbol{\Delta}^{(l-1)}] = \bigoplus_k [\widetilde{\mathbf{D}}_k] = \left[\mathbf{D}^{(l-1)} \odot_\text{EW} (\mathbf{A}_\mathcal{B}^{(l-1)})'\right]$$

**3b. 计算层 $l-1$ 权重梯度（当 $l-1 \geq 1$）**

$$[\nabla_{\mathbf{W}^{(l-1)}}] = -\frac{\eta}{B} \cdot \Pi_\text{SCM}\!\left(\langle \mathbf{A}_\mathcal{B}^{(l-2)} \rangle^\top,\; [\boldsymbol{\Delta}^{(l-1)}]\right) \in \mathbb{R}^{n_{l-1} \times n_l}$$

$$[\nabla_{\mathbf{b}^{(l-1)}}] = -\frac{\eta}{B} \cdot \bigoplus_{i=1}^{B} [\boldsymbol{\Delta}_i^{(l-1)}]$$

（同理加 L2 正则化梯度项，若启用）

---

#### 步骤 4：层 0 权重梯度（按方分块计算）

层 0 的输入是各方明文样本，$[\boldsymbol{\Delta}^{(0)}]$ 为密文，每方 $P_k$ 计算本地特征对应的梯度块（PHE 明文-密文矩阵乘）：

$$[\nabla_{\mathbf{W}^{(0,k)}}] = -\frac{\eta}{B} \cdot (\mathbf{X}_\mathcal{B}^{(k)})^\top \cdot [\boldsymbol{\Delta}^{(0)}] \in \mathbb{R}^{d_k \times n_1}$$

$$\left([\nabla_{W^{(0,k)}}]_{fj} = -\frac{\eta}{B} \sum_{i=1}^{B} x^{(k)}_{\mathcal{B},if} \odot [\Delta^{(0)}_{ij}]\right)$$

被动方 $P_k$ 将结果发送给 $P_0$，$P_0$ 按行拼装并广播完整梯度矩阵：

$$[\nabla_{\mathbf{W}^{(0)}}] = \begin{bmatrix} [\nabla_{\mathbf{W}^{(0,0)}}] \\ [\nabla_{\mathbf{W}^{(0,1)}}] \\ \vdots \\ [\nabla_{\mathbf{W}^{(0,m-1)}}] \end{bmatrix} \in \mathbb{R}^{d \times n_1}$$

$$[\nabla_{\mathbf{b}^{(0)}}] = -\frac{\eta}{B} \cdot \bigoplus_{i=1}^{B} [\boldsymbol{\Delta}_i^{(0)}]$$

---

### 5.4 加密权重更新

对所有层 $l = 0, 1, \ldots, L-1$，同态原地加更新：

$$[\mathbf{W}^{(l)}] \leftarrow [\mathbf{W}^{(l)}] \oplus [\nabla_{\mathbf{W}^{(l)}}]$$

$$[\mathbf{b}^{(l)}] \leftarrow [\mathbf{b}^{(l)}] \oplus [\nabla_{\mathbf{b}^{(l)}}]$$

---

### 5.5 精度管理（后处理）

由于每次同态运算会使密文的定点小数精度位数累积增长，需要统一对齐或截断。设当前最大精度为 $\tau_\max$，截断阈值为 $\tau_\text{max\_allowed}$，目标精度为 $\tau_\text{target}$：

$$\text{若 } \tau_\max > \tau_\text{max\_allowed}:\quad [\mathbf{W}^{(l)}] \leftarrow \Pi_\text{Trunc}\!\left([\mathbf{W}^{(l)}],\; \tau_\text{target}\right) \quad \forall l$$

$$\text{否则：}\quad [\mathbf{W}^{(l)}] \leftarrow \Pi_\text{IncPrec}\!\left([\mathbf{W}^{(l)}],\; \tau_\max\right) \quad \forall l$$

（$\Pi_\text{Trunc}$ 通过 $P_0$ 协作解密 → 截断明文 → 重新加密实现；$\Pi_\text{IncPrec}$ 通过 $P_0$ 同态调整精度位移量后广播）

---

## 六、前向传播整体数据流（以 $L=3$ 三层权重网络为例，含 DP 模块）

```
                 ┌──────────────────────────────────────────────────────┐
  PHE域(加密)    │ [W^(0)],[b^(0)]  [W^(1)],[b^(1)]  [W^(2)],[b^(2)] │
                 └──────────────────────────────────────────────────────┘

 party P_k 持有:  X_B^(k)  (明文)

 ─── 层 0（含 DP 裁剪与噪声注入，每方独立处理）────────────────────────
 各方P_k:  [Z_B^(0,k)] = X_B^(k) · [W^(0,k)]            ← 明文×密文

  ╔══════ Π_DPClip（对每个k=0..m-1执行，全部m方共同参与）══════════╗
  ║  Π_C2S:  [Z_B^(0,k)] → <Z_B^(0,k)>                ← 密文转共享 ║
  ║  SPDZ:   for each sample i in [B]:                              ║
  ║    (2a)   <q_i^(k)> = Σ_j <z_ij^(0,k)>²           ← 平方求和  ║
  ║    (2b)   <r_i^(k)> ≈ 1/√(<q_i^(k)>+ε)            ← Newton迭代 ║
  ║    (2c)   <s_i^(k)> = min(1, C·<r_i^(k)>)         ← 裁剪因子  ║
  ║    (2d)   <z̃_ij^(0,k)> = <s_i^(k)>·<z_ij^(0,k)>  ← 逐元素缩放 ║
  ║  加噪:   P_j 本地(无通信):                                       ║
  ║           <ẑ_B^(0,k)>_j += N_j^(k)                ← 明文噪声  ║
  ║           N_j^(k) ~ N(0, σ²_dp/m · I)             ← 高斯可加性 ║
  ║  Π_S2C:  <ẑ_B^(0,k)> → [ẑ_B^(0,k)]               ← 共享转密文 ║
  ╚═════════════════════════════════════════════════════════════════╝

 P_0汇总:  [Ẑ_B^(0)]  = ⊕_k [ẑ_B^(0,k)] ⊕ [b^(0)]    ← 同态聚合
 Π_C2S:    <Ẑ_B^(0)>                                    ← 转秘密共享
 SPDZ:     <A_B^(0)>, <(A_B^(0))'>                      ← MPC激活

 ─── 层 1 ──────────────────────────────────────────────────────────────
 Π_SCM:    [Z_B^(1)]   = <A_B^(0)> · [W^(1)] ⊕ [b^(1)] ← 共享×密文
 Π_C2S:    <Z_B^(1)>                                     ← 转秘密共享
 SPDZ:     <A_B^(1)>, <(A_B^(1))'>                       ← MPC激活

 ─── 层 2 (输出层) ─────────────────────────────────────────────────────
 Π_SCM:    [Z_B^(2)]   = <A_B^(1)> · [W^(2)] ⊕ [b^(2)] ← 共享×密文
 Π_C2S:    <Z_B^(2)>                                     ← 转秘密共享
 SPDZ:     <Ŷ_B> = <A_B^(2)>                            ← MPC激活
 Π_S2C:    [Ŷ_B]                                         ← 转密文(供反传)
```

---

## 七、反向传播整体数据流（同例 $L=3$）

```
 输出层 delta:
   P_0: [Δ^(2)] = [Ŷ_B] ⊕ (-1)⊙[y_B]               ← 明文标签加密做差

 ─── 层 2 梯度 ───────────────────────────────────────────────────────
   Π_SCM: [∇W^(2)] = -(η/B) <A_B^(1)>^T · [Δ^(2)]   ← 共享×密文
   P_0:   [∇b^(2)] = -(η/B) ⊕_i [Δ^(2)_i]           ← 同态求和

 ─── 层 2→1 delta回传 ────────────────────────────────────────────────
   Π_C2S:  <Δ^(2)>  ← 密文转秘密共享
   Π_SCM:  [D^(1)] = <Δ^(2)> · [W^(2)]^T             ← 共享×密文
   逐元素: [Δ^(1)] = [D^(1)] ⊙_EW <(A_B^(1))'>       ← 密文⊙共享

 ─── 层 1 梯度 ───────────────────────────────────────────────────────
   Π_SCM: [∇W^(1)] = -(η/B) <A_B^(0)>^T · [Δ^(1)]   ← 共享×密文
   P_0:   [∇b^(1)] = -(η/B) ⊕_i [Δ^(1)_i]

 ─── 层 1→0 delta回传 ────────────────────────────────────────────────
   Π_C2S:  <Δ^(1)>
   Π_SCM:  [D^(0)] = <Δ^(1)> · [W^(1)]^T
   逐元素: [Δ^(0)] = [D^(0)] ⊙_EW <(A_B^(0))'>

 ─── 层 0 梯度（按方分块） ───────────────────────────────────────────
   各方P_k: [∇W^(0,k)] = -(η/B) (X_B^(k))^T · [Δ^(0)] ← 明文×密文
   P_0汇总广播 ∇W^(0)

 ─── 权重更新 ────────────────────────────────────────────────────────
   [W^(l)] ← [W^(l)] ⊕ [∇W^(l)],  [b^(l)] ← [b^(l)] ⊕ [∇b^(l)]
   后处理精度对齐/截断
```

---

## 八、隐私保证小结

| 操作 | 数据形态 | 各方可见性 |
|------|----------|-----------|
| 特征 $\mathbf{X}^{(k)}$ | 明文 | 仅 $P_k$ 可见 |
| 标签 $\mathbf{y}$ | 明文 | 仅 $P_0$ 可见 |
| 权重 $\mathbf{W}^{(l)}$, $\mathbf{b}^{(l)}$ | 密文 $[\cdot]$ | 所有方持有密文，无法解密（需门限合作） |
| 层 0 局部贡献 $\mathbf{Z}_\mathcal{B}^{(0,k)}$（裁剪前）| 密文 → 秘密共享 → 密文 | $\Pi_\text{DPClip}$ 内以共享处理，无方可见明文 |
| 层 0 局部贡献 $\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}$（裁剪加噪后）| 密文 $[\cdot]$ | DP 保护后聚合；$P_0$ 只见含噪密文 |
| 层预激活 $\mathbf{Z}_\mathcal{B}^{(l)}$（$l \geq 1$）| 秘密共享 $\langle\cdot\rangle$ | 各方仅持有随机份额，单独无意义 |
| 激活值 $\mathbf{A}_\mathcal{B}^{(l)}$ | 秘密共享 $\langle\cdot\rangle$ | 同上；通过 SPDZ 在份额上计算 |
| 梯度 $\nabla_{\mathbf{W}^{(l)}}$ | 密文 $[\cdot]$ | 所有方持有密文，更新全程同态 |
| delta $\boldsymbol{\Delta}^{(l)}$ | 密文 $[\cdot]$ | 所有方持有密文，无法单独解密 |

整个训练过程中，**原始特征、标签、中间激活值、梯度均不以明文形式在各方之间传递**，并通过 DP 裁剪与噪声增强了特征贡献的隐私保护。

---

## 九、差分隐私保证与参数选取

### 9.1 保护语义：特征贡献级 DP vs 梯度级 DP

本方案对**分裂层激活贡献**施加 DP，与 DP-SGD 保护梯度不同：

| 维度 | 本方案（特征贡献级 DP）| DP-SGD（梯度级 DP）|
|------|----------------------|--------------------|
| 裁剪对象 | 层 0 各方局部激活 $\mathbf{z}_{\mathcal{B},i}^{(0,k)}$ | 每样本梯度 $\nabla f(\mathbf{w}; \mathbf{x}_i)$ |
| DP 保护目标 | 防止其他方从聚合贡献推断 $P_k$ 的原始特征 | 防止模型参数泄露训练集成员信息 |
| 噪声注入点 | 前向传播：层 0 聚合前 | 反向传播：梯度聚合前 |
| 适用威胁模型 | 特征重建攻击、协同推断攻击 | 成员推断攻击、模型逆向攻击 |

两者语义互补，**可叠加使用**（本方案 + 反向梯度 DP 噪声），形成双层防护。

### 9.2 单次机制的 DP 保证

对每一方 $P_k$，在一次前向传播中处理批次 $\mathcal{B}$：

- **敏感度界**：由于逐样本 L2 裁剪，单样本贡献向量 $\hat{\mathbf{z}}_{\mathcal{B},i}^{(0,k)}$ 满足 $\|\hat{\mathbf{z}}_{\mathcal{B},i}^{(0,k)}\|_2 \leq C$。将 $\mathcal{B}$ 中任意一个样本替换，$\hat{\mathbf{Z}}_\mathcal{B}^{(0,k)}$ 中对应行变化的 L2 范数不超过 $2C$（邻近数据集定义下）。

- **Gaussian 机制保证**：对噪声标准差 $\sigma_\text{dp}$ 满足

$$\sigma_\text{dp} \geq \frac{\sqrt{2 \ln(1.25/\delta)} \cdot 2C}{\epsilon}$$

则单次迭代对 $P_k$ 的输入数据集提供 $(\epsilon,\delta)$-差分隐私。

### 9.3 多次迭代的隐私预算累积

经 $T$ 次迭代训练、数据集大小 $n$、批次采样率 $q = B/n$，利用**矩量会计（Moments Accountant）** 或 **Rényi DP（RDP）** 进行组合（Abadi et al., 2016）：

$$\epsilon(T, \delta) \approx \min_{\alpha > 1} \left\{ \frac{\alpha \cdot T q^2}{(\alpha - 1)\sigma_\text{dp}^2 / (2C)^2} + \frac{\ln(1/\delta)}{\alpha - 1} \right\}$$

实用近似（$\delta = 10^{-5}$）：

$$\sigma_\text{dp} \approx \frac{2C \cdot q \sqrt{T \ln(1/\delta)}}{\epsilon}$$

建议使用 [google/dp-accounting](https://github.com/google/dp-accounting) 库精确计算隐私预算。

### 9.4 对后门攻击的抑制分析

逐样本裁剪对基于激活操控的后门攻击（Activation-level Attack）有以下抑制效果：

- **限制单样本影响力**：攻击者注入的中毒样本经裁剪后，其在层 0 聚合 $[\hat{\mathbf{Z}}_\mathcal{B}^{(0)}]$ 中的贡献 L2 范数被强制压制到 $\leq C$，大幅降低后门模式的"激活增益"

- **高斯噪声干扰**：叠加的随机噪声使对手难以精确控制激活值以实现后门触发，提高攻击不稳定性

- **局限性**：若后门样本的激活范数与正常样本相近（隐蔽型后门），裁剪效果有限；且本方案仅保护层 0，更高层的后门操控不受此约束

### 9.5 超参数选取建议

| 参数 | 推荐做法 | 备注 |
|------|----------|------|
| $C$（裁剪阈值）| 初始设为训练集中 $\|\mathbf{z}_{\mathcal{B},i}^{(0,k)}\|_2$ 的中位数 | 过小影响收敛，过大削弱 DP 效果 |
| $\sigma_\text{dp}$（噪声强度）| 由 $(\epsilon, \delta, T, q)$ 通过 dp-accounting 反推 | 通常 $\sigma_\text{dp} \in [0.5C, 5C]$ |
| $T_\text{NR}$（Newton 迭代次数）| $2 \sim 3$ 步（定点精度下已足够）| 需与 SPDZ 定点小数精度配合 |
| $\varepsilon$（零除保护）| $10^{-6}$ 量级 | 防止 $\|\mathbf{z}_i\|_2 \approx 0$ 时数值爆炸 |
| 各方噪声分配 | 均分：$\sigma_j = \sigma_\text{dp}/\sqrt{m}$ | 等效于单一 $\mathcal{N}(0, \sigma_\text{dp}^2\mathbf{I})$，可按信任度加权分配 |

---

## 十、拆分学习视角的等价表示

> **说明**：以下将现有 Falcon VFL-MLP 训练流程**原封不动**地改写为拆分学习（Split Learning）惯用的"底层模型 / 顶层模型"两段式符号框架，不改变任何实现逻辑与协议细节，仅重新组织描述层次。

---

### 10.1 模型划分定义

**分割点（Cut Layer）**：层 0 的输出，即各方局部特征经加噪裁剪并同态聚合后得到的 $[\hat{\mathbf{Z}}_\mathcal{B}^{(0)}]$（或经 $\Pi_\text{C2S}$ 后的 $\langle\hat{\mathbf{Z}}_\mathcal{B}^{(0)}\rangle$）。

$$\text{分割位置：} \quad \underbrace{\mathbf{X}^{(k)} \xrightarrow{\text{底层模型}} \hat{\mathbf{Z}}^{(0)}}_{\text{各方本地 + 跨方聚合}} \;\Big|\; \underbrace{\hat{\mathbf{Z}}^{(0)} \xrightarrow{\text{顶层模型}} \hat{\mathbf{Y}}}_{\text{主动方 }P_0\text{ 主导}}$$

#### 底层模型（Bottom Model，$f_\text{bot}$）

- **所有者**：各参与方 $P_k$，$k = 0, 1, \ldots, m-1$
- **参数**：各方本地权重块 $\{\mathbf{W}^{(0,k)}, \mathbf{b}^{(0)}\}$（层 0 各方分块 + 共享偏置）
- **功能**：将原始特征映射到分割点的聚合嵌入

$$f_\text{bot}^{(k)}(\mathbf{X}^{(k)};\, \mathbf{W}^{(0,k)}) = \mathbf{X}^{(k)} \cdot \mathbf{W}^{(0,k)} \in \mathbb{R}^{B \times n_1}$$

各方底层模型输出经 DP 裁剪与噪声注入后聚合，形成分割点嵌入：

$$\hat{\mathbf{Z}}^{(0)} = \sigma_0\!\left(\bigoplus_{k=0}^{m-1} \Pi_\text{DPClip}\!\left([f_\text{bot}^{(k)}(\mathbf{X}^{(k)};\,\mathbf{W}^{(0,k)})]\right) \oplus [\mathbf{b}^{(0)}]\right)$$

> **注意**：Falcon 实现中，底层模型计算（含 DP 裁剪）均在加密域进行，各方参数 $\mathbf{W}^{(0,k)}$ 以 PHE 密文形式存储，各方明文操作仅限于自身特征 $\mathbf{X}^{(k)}$（与密文参数做明文-密文矩阵乘）。

#### 顶层模型（Top Model，$f_\text{top}$）

- **所有者**：主动方 $P_0$（持有标签）主导协调，所有方联合参与计算
- **参数**：层 $1$ 至层 $L-1$ 的权重 $\{\mathbf{W}^{(l)}, \mathbf{b}^{(l)}\}_{l=1}^{L-1}$
- **功能**：从分割点嵌入映射至最终预测

$$f_\text{top}(\hat{\mathbf{Z}}^{(0)};\, \{\mathbf{W}^{(l)}, \mathbf{b}^{(l)}\}_{l=1}^{L-1}) = \hat{\mathbf{Y}} \in \mathbb{R}^{B \times n_L}$$

顶层模型由层 $1$ 到层 $L-1$ 串联形成，每层使用 $\Pi_\text{SCM}$ + $\Pi_\text{C2S}$ + SPDZ-Activation。

---

### 10.2 前向传播（拆分学习视角）

#### 阶段 A：底层模型前向（各方本地 → 分割点，跨方聚合）

$$\text{各方 }P_k\text{ 并行执行：} \quad [\mathbf{Z}^{(0,k)}] = \mathbf{X}_\mathcal{B}^{(k)} \cdot [\mathbf{W}^{(0,k)}]$$

$$\text{各方联合执行 DP 裁剪：} \quad [\hat{\mathbf{Z}}^{(0,k)}] = \Pi_\text{DPClip}\!\left([\mathbf{Z}^{(0,k)}],\, C,\, \sigma_\text{dp}\right)$$

$$\text{被动方上传至}P_0\text{，}P_0\text{同态聚合：}\quad [\hat{\mathbf{Z}}^{(0)}] = \bigoplus_{k} [\hat{\mathbf{Z}}^{(0,k)}] \oplus [\mathbf{b}^{(0)}]$$

$$\text{分割点激活（SPDZ）：} \quad \langle \mathbf{A}^{(0)} \rangle,\, \langle (\mathbf{A}^{(0)})' \rangle \leftarrow \text{SPDZ-Activation}\!\left(\Pi_\text{C2S}([\hat{\mathbf{Z}}^{(0)}]),\; \sigma_0\right)$$

> **分割点传输内容**：$\langle \mathbf{A}^{(0)} \rangle$（激活值秘密共享）由 SPDZ 引擎分发至各方，顶层模型即从此份额开始计算，**不传输明文激活**。

#### 阶段 B：顶层模型前向（分割点 → 预测，均由各方联合计算）

对 $l = 1, 2, \ldots, L-1$，循环执行：

$$[\mathbf{Z}^{(l)}] = \Pi_\text{SCM}\!\left(\langle \mathbf{A}^{(l-1)} \rangle,\; [\mathbf{W}^{(l)}]\right) \oplus [\mathbf{b}^{(l)}]$$

$$\langle \mathbf{A}^{(l)} \rangle,\, \langle (\mathbf{A}^{(l)})' \rangle \leftarrow \text{SPDZ-Activation}\!\left(\Pi_\text{C2S}([\mathbf{Z}^{(l)}]),\; \sigma_l\right)$$

最终预测：$[\hat{\mathbf{Y}}] \leftarrow \Pi_\text{S2C}(\langle \mathbf{A}^{(L-1)} \rangle)$

---

### 10.3 反向传播（拆分学习视角）

#### 阶段 C：顶层模型反向（损失 → 分割点梯度）

**损失与输出 delta**（$P_0$ 计算并广播）：

$$[\boldsymbol{\Delta}^{(L-1)}] = [\hat{\mathbf{Y}}] \oplus (-1)\odot[\mathbf{y}]$$

**顶层模型各层梯度与 delta 回传**（$l$ 从 $L-1$ 降至 $1$）：

$$[\nabla_{\mathbf{W}^{(l)}}] = -\frac{\eta}{B}\cdot\Pi_\text{SCM}\!\left(\langle\mathbf{A}^{(l-1)}\rangle^\top,\; [\boldsymbol{\Delta}^{(l)}]\right)$$

$$[\boldsymbol{\Delta}^{(l-1)}] = \Pi_\text{SCM}\!\left(\Pi_\text{C2S}([\boldsymbol{\Delta}^{(l)}]),\; [\mathbf{W}^{(l)}]^\top\right) \odot_\text{EW} \langle(\mathbf{A}^{(l-1)})'\rangle \quad (l \geq 2)$$

**分割点梯度**：当 $l=1$ 时，上述回传得到 $[\boldsymbol{\Delta}^{(0)}]$，即**分割点误差信号**（等价于标准拆分学习中从顶层模型传回底层模型的"切割层梯度"）。

> 在 Falcon 中，$[\boldsymbol{\Delta}^{(0)}]$ 以 PHE **密文**形式广播给所有方，而非明文梯度——这是 Falcon 相对标准拆分学习的核心隐私增强。

#### 阶段 D：底层模型反向（分割点梯度 → 各方本地参数更新）

$P_0$ 将 $[\boldsymbol{\Delta}^{(0)}]$ 广播后，各方 $P_k$ 用本地明文特征对密文 delta 做转置矩阵乘（明文×密文），计算自己底层参数的加密梯度：

$$[\nabla_{\mathbf{W}^{(0,k)}}] = -\frac{\eta}{B}\cdot(\mathbf{X}_\mathcal{B}^{(k)})^\top \cdot [\boldsymbol{\Delta}^{(0)}] \in \mathbb{R}^{d_k \times n_1}$$

各方在本地对自身底层权重做同态更新，**无需将梯度上传给任何其他方**：

$$[\mathbf{W}^{(0,k)}] \leftarrow [\mathbf{W}^{(0,k)}] \oplus [\nabla_{\mathbf{W}^{(0,k)}}]$$

---

### 10.4 拆分学习 vs 标准实现对照表

| 拆分学习概念 | Falcon 中的等价实现 | 安全增强点 |
|-------------|-------------------|-----------|
| 切割层（Cut Layer）输出 | $\langle\mathbf{A}^{(0)}\rangle$（SPDZ 秘密共享） | 明文激活从不暴露给任何单一方 |
| 切割层梯度（Smashed Gradient）| $[\boldsymbol{\Delta}^{(0)}]$（PHE 密文） | 梯度以密文回传，防止梯度反演攻击 |
| 底层模型参数 | $[\mathbf{W}^{(0,k)}]$（PHE 密文，各方本地） | 参数全程加密，无中心聚合 |
| 顶层模型参数 | $\{[\mathbf{W}^{(l)}]\}_{l=1}^{L-1}$（PHE 密文，广播） | 所有方持有相同密文副本，需协作解密 |
| 底层→顶层通信 | $\langle\mathbf{A}^{(0)}\rangle$ 通过 SPDZ 分发 | 秘密共享传输，无明文激活 |
| 顶层→底层通信 | $[\boldsymbol{\Delta}^{(0)}]$ 由 $P_0$ 广播 | 加密梯度，而非标准拆分学习的明文梯度 |
| 各方底层独立更新 | 各方用 $(\mathbf{X}^{(k)})^\top \cdot [\boldsymbol{\Delta}^{(0)}]$ 本地更新 $[\mathbf{W}^{(0,k)}]$ | 梯度不跨方传递 |
| 激活值 DP 保护 | $\Pi_\text{DPClip}$（裁剪 + 高斯噪声）在切割层输出前执行 | 防止从聚合激活重建各方原始特征 |

---

### 10.5 整体拓扑图（拆分学习视角）

```
  ┌──────────────────────────────────────────────────────────────────┐
  │                     底层模型（Bottom Model）                      │
  │  P_0: X^(0) ──→ [X^(0)·W^(0,0)] ──┐                            │
  │  P_1: X^(1) ──→ [X^(1)·W^(0,1)] ──┤ Π_DPClip(各方)             │
  │   ⋮                                ⋮                            │
  │  P_{m-1}: X^(m-1)→[X^(m-1)·W^(0,m-1)]┘                        │
  │                        │                                        │
  │           ⊕ 同态聚合 + [b^(0)] (P_0)                            │
  │           ↓                                                     │
  │        SPDZ-Activation → <A^(0)>  ←── 分割点（Cut Layer）        │
  └──────────────────────────────────────┬───────────────────────────┘
                                         │ <A^(0)>（秘密共享，无明文）
                                         ↓
  ┌──────────────────────────────────────────────────────────────────┐
  │                     顶层模型（Top Model）                         │
  │   层1: Π_SCM(<A^(0)>, [W^(1)]) ⊕ [b^(1)] → Π_C2S → SPDZ       │
  │   层2: Π_SCM(<A^(1)>, [W^(2)]) ⊕ [b^(2)] → Π_C2S → SPDZ       │
  │   ⋮                                                             │
  │   输出层: Π_S2C(<A^(L-1)>) → [Ŷ]                                │
  │                        │                                        │
  │   损失: [Δ^(L-1)] = [Ŷ] ⊕ (-1)⊙[y]   (P_0 持有标签)            │
  │   反传: [Δ^(l-1)] ← 各层 Π_SCM + 逐元素⊙ 导数共享               │
  │                        │                                        │
  │   分割点梯度: [Δ^(0)]（PHE密文广播）  ← 切割层梯度（加密）         │
  └──────────────────────────────────────┬───────────────────────────┘
                                         │ [Δ^(0)]（密文，非明文梯度）
                                         ↓
  ┌──────────────────────────────────────────────────────────────────┐
  │                   底层模型反向（各方独立更新）                     │
  │  P_k: [∇W^(0,k)] = -(η/B)(X^(k))^T · [Δ^(0)]  (本地明文×密文)  │
  │        [W^(0,k)] ← [W^(0,k)] ⊕ [∇W^(0,k)]     (本地同态更新)   │
  └──────────────────────────────────────────────────────────────────┘
```
