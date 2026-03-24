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

#### 层 0（第一个权重层，跨方计算）

因各方只持有部分特征，每方 $P_k$ 本地计算其特征对应的局部聚合（PHE 明文-密文矩阵乘）：

$$[\mathbf{Z}_\mathcal{B}^{(0,k)}] = \mathbf{X}_\mathcal{B}^{(k)} \cdot [\mathbf{W}^{(0,k)}] \in \mathbb{R}^{B \times n_1}, \quad \left([Z^{(0,k)}_{\mathcal{B},ij}] = \sum_{f=1}^{d_k} x^{(k)}_{\mathcal{B},if} \odot [W^{(0,k)}_{fj}]\right)$$

被动方 $P_k$ ($k \neq 0$) 序列化并上传至 $P_0$：

$$P_k \xrightarrow{[\mathbf{Z}_\mathcal{B}^{(0,k)}]} P_0$$

主动方 $P_0$ 聚合所有局部结果，同态加入偏置：

$$[\mathbf{Z}_\mathcal{B}^{(0)}] = \bigoplus_{k=0}^{m-1} [\mathbf{Z}_\mathcal{B}^{(0,k)}] \oplus \mathbf{1}_B \otimes [\mathbf{b}^{(0)}] \in \mathbb{R}^{B \times n_1}$$

$P_0$ 广播 $[\mathbf{Z}_\mathcal{B}^{(0)}]$，然后执行密文转秘密共享：

$$\langle \mathbf{Z}_\mathcal{B}^{(0)} \rangle \leftarrow \Pi_\text{C2S}\!\left([\mathbf{Z}_\mathcal{B}^{(0)}]\right)$$

通过 SPDZ 联合计算激活函数及其导数的秘密共享：

$$\langle \mathbf{A}_\mathcal{B}^{(0)} \rangle, \; \langle (\mathbf{A}_\mathcal{B}^{(0)})' \rangle \leftarrow \text{SPDZ-Activation}\!\left(\langle \mathbf{Z}_\mathcal{B}^{(0)} \rangle,\; \sigma_0\right)$$

$$\text{其中 } \mathbf{A}_\mathcal{B}^{(0)} = \sigma_0\!\left(\mathbf{Z}_\mathcal{B}^{(0)}\right) \in \mathbb{R}^{B \times n_1}, \quad (\mathbf{A}_\mathcal{B}^{(0)})' = \sigma_0'\!\left(\mathbf{Z}_\mathcal{B}^{(0)}\right) \in \mathbb{R}^{B \times n_1}$$

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

## 六、前向传播整体数据流（以 $L=3$ 三层权重网络为例）

```
                 ┌──────────────────────────────────────────────────────┐
  PHE域(加密)    │ [W^(0)],[b^(0)]  [W^(1)],[b^(1)]  [W^(2)],[b^(2)] │
                 └──────────────────────────────────────────────────────┘

 party P_k 持有:  X_B^(k)  (明文)

 ─── 层 0 ──────────────────────────────────────────────────────────────
 各方P_k:  [Z_B^(0,k)] = X_B^(k) · [W^(0,k)]            ← 明文×密文
 P_0汇总:  [Z_B^(0)]   = ⊕_k [Z_B^(0,k)] ⊕ [b^(0)]     ← 同态聚合
 Π_C2S:    <Z_B^(0)>                                     ← 转秘密共享
 SPDZ:     <A_B^(0)>, <(A_B^(0))'>                       ← MPC激活

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
| 层预激活 $\mathbf{Z}_\mathcal{B}^{(l)}$ | 秘密共享 $\langle\cdot\rangle$ | 各方仅持有随机份额，单独无意义 |
| 激活值 $\mathbf{A}_\mathcal{B}^{(l)}$ | 秘密共享 $\langle\cdot\rangle$ | 同上；通过 SPDZ 在份额上计算 |
| 梯度 $\nabla_{\mathbf{W}^{(l)}}$ | 密文 $[\cdot]$ | 所有方持有密文，更新全程同态 |
| delta $\boldsymbol{\Delta}^{(l)}$ | 密文 $[\cdot]$ | 所有方持有密文，无法单独解密 |

整个训练过程中，**原始特征、标签、中间激活值、梯度均不以明文形式在各方之间传递**，从而实现了纵向联邦学习下的完整隐私保护。
