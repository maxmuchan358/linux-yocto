# CPU例外時のレジスタ取得タイミング

## 1) カスタム追加ダンプ (CONFIG_CUSTOM_CRASHCUMP)

```mermaid
sequenceDiagram
    participant PC as panic CPU
    participant OC as 他CPU
    participant NC as NMI CPU

    Note over PC,NC: 起点: CPU例外発生
    PC->>PC: __die_body(regs) で kdmp_ecxt_regs[cpu] = regs
    Note over PC: ここでは pt_regs ポインタ保存のみ (実体コピーなし)

    alt kexec_should_crash=1
        alt crash_kexec_post_notifiers=0
            PC->>PC: oops_end で crash_kexec(regs)
            PC->>PC: __crash_kexec(regs) に即時遷移
            Note over PC: panic/vpanic を経由しないため panic_dump_gprs は未実行
            Note over PC: panic CPUの custom status[panic] へのGPR保存処理は走らない

            PC->>OC: machine_crash_shutdown -> crash_smp_send_stop
            OC->>OC: crash_nmi_callback(regs)
            OC->>OC: kdmp_ipi_regs[cpu] = regs
            OC->>OC: ipi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_IPI)
            Note over OC: kdmp_dump_gprs: inline asmでGPR読取 -> status[IPI].x86_regs.gprs/saved_sp
            Note over OC: kdmp_dump_x86: memcpy(*kdmp_ipi_regs)->excp_gprs + CR/DR/MMX/XMM/MSR/kstack

            NC->>NC: do_nmi(regs) (外部NMI/WD)
            NC->>NC: kdmp_nmi_regs[cpu] = regs
            NC->>NC: nmi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_NMI)
            Note over NC: kdmp_dump_gprs: inline asmでGPR読取 -> status[NMI].x86_regs.gprs/saved_sp
            Note over NC: kdmp_dump_x86: memcpy(*kdmp_nmi_regs)->excp_gprs + CR/DR/MMX/XMM/MSR/kstack
        else crash_kexec_post_notifiers=1
            PC->>PC: oops_end では __crash_kexec せず panic/vpanic へ
            PC->>PC: panic_dump_gprs() -> dump_call_panic()
            PC->>PC: kdmp_dump_gprs(KDMP_PANIC)
            PC->>PC: kdmp_dump_x86(KDMP_PANIC)
            Note over PC: kdmp_dump_gprs: inline asmでGPR読取 -> status[PANIC].x86_regs.gprs/saved_sp
            Note over PC: kdmp_dump_x86: memcpy(*kdmp_ecxt_regs)->excp_gprs + CR/DR/MMX/XMM/MSR/kstack

            PC->>OC: crash_smp_send_stop() で crash NMI送信
            OC->>OC: crash_nmi_callback(regs)
            OC->>OC: kdmp_ipi_regs[cpu] = regs
            OC->>OC: ipi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_IPI)
            Note over OC: status[IPI] に gprs/excp_gprs を保存

            NC->>NC: do_nmi(regs) (外部NMI/WD)
            NC->>NC: kdmp_nmi_regs[cpu] = regs
            NC->>NC: nmi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_NMI)
            Note over NC: status[NMI] に gprs/excp_gprs を保存

            PC->>PC: panic_notifierで kdmp_panicdump_exec()
            PC->>PC: __crash_kexec() で kdump kernelへ
        end
    else kexec_should_crash=0
        alt panicする場合
            PC->>PC: oops_end -> panic/vpanic
            PC->>PC: panic_dump_gprs() -> dump_call_panic()
            PC->>PC: kdmp_dump_gprs(KDMP_PANIC)
            Note over PC: vpanic文脈のGPRを取得
            PC->>PC: kdmp_dump_x86(KDMP_PANIC)
            Note over PC: dump_ecxt_gprs()で *kdmp_ecxt_regs を excp_gprs にコピー
            Note over PC: status[PANIC].x86_regs.gprs/saved_sp/excp_gprs に格納

            PC->>OC: crash_smp_send_stop() で crash NMI送信
            OC->>OC: crash_nmi_callback(regs)
            OC->>OC: kdmp_ipi_regs[cpu] = regs
            OC->>OC: ipi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_IPI)
            Note over OC: IPI受信文脈GPR + 割込時pt_regsを保存
            Note over OC: 保存先は status[IPI].x86_regs.gprs/excp_gprs

            NC->>NC: do_nmi(regs) (外部NMI/WD)
            NC->>NC: kdmp_nmi_regs[cpu] = regs
            NC->>NC: nmi_dump_gprs() -> kdmp_dump_gprs/x86(KDMP_NMI)
            Note over NC: NMI文脈GPR + NMI時pt_regsを保存
            Note over NC: 保存先は status[NMI].x86_regs.gprs/excp_gprs

            PC->>PC: panic_notifierで kdmp_panicdump_exec()
            PC->>PC: __crash_kexec() で kdump kernelへ
        else panicしない場合
            PC->>PC: notify_die(DIE_OOPS)==NOTIFY_STOP または panic条件不成立
            PC->>PC: oops_end復帰/タスク終了
            Note over PC,NC: カスタムダンプ領域へのレジスタ保存は走らない
        end
    end

    Note over PC,NC: 競合時は dump_check_call_status() が -EBUSY を返し重複保存を抑止
```

## 2) 標準 vmcore (ELF crash notes)

```mermaid
sequenceDiagram
    participant PC as panic CPU
    participant OC as 他CPU
    participant NC as NMI CPU

    Note over PC,NC: 起点: CPU例外発生

    alt 例外から直接 __crash_kexec する場合
        PC->>PC: oops_end(regs)
        PC->>PC: kexec_should_crash(current)==1
        PC->>PC: __crash_kexec(regs)
        PC->>PC: crash_setup_regs(&fixed_regs, regs)
        Note over PC: panic CPUは例外pt_regsを保存元にする
        Note over PC: crash_save_cpuで elf_core_copy_regs(&prstatus.pr_reg, fixed_regs)

        PC->>OC: machine_crash_shutdown() -> crash_smp_send_stop()
        OC->>OC: crash_nmi_callback(regs)
        OC->>OC: kdump_nmi_callback(regs)
        OC->>OC: crash_save_cpu(regs, cpu)
        Note over OC: crash_notes[cpu] に NT_PRSTATUS(pr_reg=pt_regs) を保存

        NC->>NC: do_nmi(regs) 中に panic CPUのcrash IPIを検知
        NC->>NC: run_crash_ipi_callback(regs)
        NC->>NC: crash_nmi_callback -> kdump_nmi_callback
        NC->>NC: crash_save_cpu(regs, cpu)
        Note over NC: crash_notes[cpu] に NT_PRSTATUS(pr_reg=pt_regs) を保存

        PC->>PC: native_machine_crash_shutdown() 末尾で crash_save_cpu(&fixed_regs, panic_cpu)
        PC->>PC: machine_kexec(kexec_crash_image)
    else panic経由で __crash_kexec する場合
        PC->>PC: oops_end では kexec_should_crash(current)==0
        PC->>PC: panic/vpanic へ進む
        PC->>PC: panic.c から __crash_kexec(NULL)
        PC->>PC: crash_setup_regs(&fixed_regs, NULL)
        Note over PC: panic CPUは現在文脈レジスタから保存元を作成
        Note over PC: crash_save_cpuで elf_core_copy_regs(&prstatus.pr_reg, fixed_regs)

        PC->>OC: machine_crash_shutdown() -> crash_smp_send_stop()
        OC->>OC: crash_nmi_callback(regs)
        OC->>OC: kdump_nmi_callback(regs)
        OC->>OC: crash_save_cpu(regs, cpu)
        Note over OC: crash_notes[cpu] に NT_PRSTATUS(pr_reg=pt_regs) を保存

        NC->>NC: do_nmi(regs) 中に panic CPUのcrash IPIを検知
        NC->>NC: run_crash_ipi_callback(regs)
        NC->>NC: crash_nmi_callback -> kdump_nmi_callback
        NC->>NC: crash_save_cpu(regs, cpu)
        Note over NC: crash_notes[cpu] に NT_PRSTATUS(pr_reg=pt_regs) を保存

        PC->>PC: native_machine_crash_shutdown() 末尾で crash_save_cpu(&fixed_regs, panic_cpu)
        PC->>PC: machine_kexec(kexec_crash_image)
    else panicしない場合
        PC->>PC: kexec_should_crash(current)==0 かつ panic条件不成立
        PC->>PC: oops_end復帰/タスク終了
        Note over PC,NC: vmcore用 crash_notes 保存も kexec遷移も発生しない
    end
```

### 2-1) 標準 vmcore (ELF crash notes, 1コアのみ)

```mermaid
sequenceDiagram
    participant PC as panic CPU (唯一のCPU)

    Note over PC: 起点: CPU例外発生

    alt 例外から直接 __crash_kexec する場合
        PC->>PC: oops_end(regs)
        PC->>PC: kexec_should_crash(current)==1
        PC->>PC: __crash_kexec(regs)
        PC->>PC: crash_setup_regs(&fixed_regs, regs)
        Note over PC: 保存元は例外時 pt_regs
        PC->>PC: machine_crash_shutdown()
        Note over PC: 他CPUがいないため crash_smp_send_stop() は停止対象なし
        PC->>PC: native_machine_crash_shutdown() 末尾で crash_save_cpu(&fixed_regs, panic_cpu)
        PC->>PC: elf_core_copy_regs(&prstatus.pr_reg, fixed_regs)
        Note over PC: crash_notes[panic_cpu] に NT_PRSTATUS を1件だけ保存
        PC->>PC: machine_kexec(kexec_crash_image)

    else panic経由で __crash_kexec する場合
        PC->>PC: oops_end では kexec_should_crash(current)==0
        PC->>PC: panic/vpanic へ進む
        PC->>PC: panic.c から __crash_kexec(NULL)
        PC->>PC: crash_setup_regs(&fixed_regs, NULL)
        Note over PC: 保存元は panic 時点の現在文脈レジスタ
        PC->>PC: machine_crash_shutdown()
        Note over PC: 他CPU向け crash NMI は発生しない
        PC->>PC: native_machine_crash_shutdown() 末尾で crash_save_cpu(&fixed_regs, panic_cpu)
        PC->>PC: elf_core_copy_regs(&prstatus.pr_reg, fixed_regs)
        Note over PC: crash_notes[panic_cpu] の NT_PRSTATUS のみが vmcore に入る
        PC->>PC: machine_kexec(kexec_crash_image)

    else panicしない場合
        PC->>PC: kexec_should_crash(current)==0 かつ panic条件不成立
        PC->>PC: oops_end復帰/タスク終了
        Note over PC: crash_notes 保存も kexec遷移も発生しない
    end
```

補足:
- 1コア構成では `crash_notes[]` に有効な `NT_PRSTATUS` が 1 件だけ入り、他CPU由来の crash note は存在しない。
- direct `__crash_kexec(regs)` では例外 `pt_regs` がそのまま保存元になり、panic 経由では `crash_setup_regs(..., NULL)` が現在文脈から保存用レジスタを組み立てる。
- `machine_crash_shutdown()` は実行されるが、停止対象の他CPUがいないため `crash_smp_send_stop()` と `crash_nmi_callback()` による追加保存は起きない。

3a) LSLSM / LSNAM
```mermaid
sequenceDiagram
    participant PC as panic CPU
    participant OC as 他CPU (IPI受信)
    participant NC as NMI CPU

    Note over PC,NC: 起点: CPU例外発生
    PC->>PC: __die_body(regs)
    PC->>PC: pdmp_ecxt_regs[cpu] = regs
    Note over PC: ポインタ保存のみ (実体コピーなし)

    NC->>NC: do_nmi(regs) (外部NMI/WD)
    NC->>NC: pdmp_nmi_regs[cpu] = regs
    NC->>NC: nmi_dump_gprs() → dump_call_nmi()
    NC->>NC: pdmp_dump_gprs(PDMP_NMI)
    Note over NC: inline asmで現在GPR読取<br/>→ status[NMI].x86_regs.gprs に保存
    NC->>NC: pdmp_dump_x86(PDMP_NMI)
    NC->>NC: dump_check_call_status(NMI) で重複ガード
    NC->>NC: dump_ecxt_gprs: memcpy(*pdmp_nmi_regs) → status[NMI].x86_regs.excp_gprs
    NC->>NC: dump_kstack / dump_arch_regs(CR/DR/MMX/XMM) / dump_msrs / dump_local_apic

    Note over PC: CONFIG_PANIC_DUMP=y のため oops_end() から<br/>crash_kexec(regs) は呼ばれない (コンパイルアウト)

    alt panicする場合
        PC->>PC: oops_end() → panic() / vpanic()
        PC->>PC: panic_dump_gprs() → dump_call_panic()
        PC->>PC: pdmp_dump_gprs(PDMP_PANIC)
        Note over PC: inline asmで現在GPR読取<br/>→ status[PANIC].x86_regs.gprs に保存
        PC->>PC: pdmp_dump_x86(PDMP_PANIC)
        PC->>PC: dump_check_call_status(PANIC)
        Note over PC: pdmp_ecxt_regs[n]==NULL なら PDMP_DIR_PANIC 扱い
        PC->>PC: dump_ecxt_gprs: memcpy(*pdmp_ecxt_regs) → status[PANIC].x86_regs.excp_gprs
        PC->>PC: dump_kstack / dump_arch_regs / dump_msrs / dump_local_apic

        PC->>OC: crash_smp_send_stop() で crash NMI送信
        OC->>OC: pdmp_ipi_regs[cpu] = regs
        OC->>OC: ipi_dump_gprs() → dump_call_ipi()
        OC->>OC: pdmp_dump_gprs(PDMP_IPI)
        Note over OC: inline asmで現在GPR読取<br/>→ status[IPI].x86_regs.gprs に保存
        OC->>OC: pdmp_dump_x86(PDMP_IPI)
        OC->>OC: dump_check_call_status(IPI): pdmp_nmi_regs[n]!=NULL なら -EBUSY でスキップ
        OC->>OC: dump_ecxt_gprs: memcpy(*pdmp_ipi_regs) → status[IPI].x86_regs.excp_gprs
        OC->>OC: dump_kstack / dump_arch_regs / dump_msrs / dump_local_apic

        PC->>PC: panic notifier → pdmp_panicdump_exec()
        Note over PC: CPU レジスタ類はすでに保存済み
        PC->>PC: dump_pci_regs() / dump_io_regs()
        PC->>PC: printk_tail() → printk_buf 保存
        PC->>PC: __crash_kexec(NULL)
        PC->>PC: crash_setup_regs(&fixed_regs, NULL)
        Note over PC: 保存元は panic 時点の現在文脈レジスタ
        PC->>OC: native_machine_crash_shutdown() -> crash_smp_send_stop()
        Note over OC: 既に停止済みCPUが多く、再送は stop 処理の再確認になる
        PC->>PC: cpu_emergency_disable_virtualization()
        PC->>PC: cpu_emergency_stop_pt()
        PC->>PC: ioapic_zap_locks()/clear_IO_APIC()/lapic_shutdown()
        PC->>PC: restore_boot_irq_mode()/hpet_disable()
        PC->>PC: x86_platform.guest.enc_kexec_begin()/finish()
        PC->>PC: crash_save_cpu(&fixed_regs, panic_cpu)
        Note over PC: 標準vmcore用 crash_notes[panic_cpu] に NT_PRSTATUS を保存
        PC->>PC: wbinvd()
        PC->>PC: machine_kexec(kexec_crash_image) → kdump kernel へ

    else panicしない場合
        PC->>PC: oops_end 復帰/タスク終了
        Note over PC,NC: pdmp_panicdump_exec() は走らない
    end

    Note over PC,NC: dump_check_call_status() が -EBUSY を返し重複保存を抑止<br/>(NMI受信済みCPUへの IPI 等)
```

3b) CPCPU
```mermaid
sequenceDiagram
    participant PC as panic CPU (コア0固定)

    Note over PC: 起点: CPU例外発生
    PC->>PC: __die_body(regs)
    PC->>PC: pdmp_ecxt_regs = regs
    Note over PC: 単一ポインタ保存 (配列なし)
    Note over PC: CONFIG_PANIC_DUMP=y のため oops_end() から<br/>crash_kexec(regs) は呼ばれない (コンパイルアウト)

    alt panicする場合
        PC->>PC: oops_end() → panic()
        PC->>PC: panic_dump_gprs() → pdmp_dump_gprs()
        Note over PC: inline asmで現在GPR読取<br/>→ status[0].x86_regs.gprs に保存<br/>(PDMP_OK_GPRS フラグセット)

        PC->>PC: panic notifier → pdmp_panicdump_exec()
        PC->>PC: dump_ecxt_gprs()
        Note over PC: pdmp_ecxt_regs != NULL なら<br/>memcpy → status[0].x86_regs.excp_gprs<br/>(PDMP_OK_GPRS_EXCP フラグセット)
        PC->>PC: dump_kstack(ecxt_regsのspを使用)
        PC->>PC: setup_header() (magic/format/timestamp/sysinfo)
        PC->>PC: dump_arch_regs() (CR0/2/3/4, GDT/IDT/LDT/TR, DR0-7, FPU, MMX, XMM)
        PC->>PC: dump_msrs() (CPUID, MCMSRs, MSRs)
        PC->>PC: dump_pci_regs()
        PC->>PC: dump_io_regs()
        PC->>PC: printk_tail() → printk_buf 保存
        Note over PC: nmi_dump_gprs/ipi_dump_gprs = NULL のため<br/>他CPUのレジスタは pdump 領域に保存されない
        PC->>PC: __crash_kexec(NULL)
        PC->>PC: crash_setup_regs(&fixed_regs, NULL)
        Note over PC: 保存元は panic 時点の現在文脈レジスタ
        PC->>PC: native_machine_crash_shutdown()
        Note over PC: 単一CPUのため crash_smp_send_stop() は停止対象なし
        PC->>PC: cpu_emergency_disable_virtualization()
        PC->>PC: cpu_emergency_stop_pt()
        PC->>PC: ioapic_zap_locks()/clear_IO_APIC()/lapic_shutdown()
        PC->>PC: restore_boot_irq_mode()/hpet_disable()
        PC->>PC: x86_platform.guest.enc_kexec_begin()/finish()
        PC->>PC: crash_save_cpu(&fixed_regs, panic_cpu)
        Note over PC: 標準vmcore用 crash_notes[0] に NT_PRSTATUS を保存
        PC->>PC: wbinvd()
        PC->>PC: machine_kexec(kexec_crash_image) → kdump kernel へ

    else panicしない場合
        PC->>PC: oops_end 復帰/タスク終了
        Note over PC: pdmp_dump_gprs() も pdmp_panicdump_exec() も走らない
    end
```

補足:
- 3a/3b の `pdmp_*` 保存はカスタム追加ダンプ領域向けで、`__crash_kexec(NULL)` 以降は別系統で標準 vmcore 用 `crash_notes[]` 保存も走る。
- `native_machine_crash_shutdown()` の末尾では panic CPU について `crash_save_cpu()` が呼ばれるため、custom dump とは別に ELF crash notes の `NT_PRSTATUS` も残る。

補足:
- kexec_should_crash は標準vmcoreの分岐条件だが、カスタム追加ダンプでも「panicを経由するか」を決めるため間接的に重要。
- 特に direct __crash_kexec の場合、panic_dump_gprs と panic_notifier(kdmp_panicdump_exec) は走らない。
- crash_kexec_post_notifiers=1 の場合、例外直後の crash_kexec(regs) は抑止され、panic経路側の __crash_kexec(NULL) まで遅延する。

## 取得処理の明示 (関数と保存先)

- カスタム panic CPU GPR: kdmp_dump_gprs(KDMP_PANIC) が inline asm でGPRを読み、status[PANIC].x86_regs.gprs/saved_sp へ保存。
- カスタム panic CPU 例外pt_regs: dump_ecxt_gprs() が memcpy(*kdmp_ecxt_regs[cpu]) を status[PANIC].x86_regs.excp_gprs へ保存。
- カスタム 他CPU: crash_nmi_callback で kdmp_ipi_regs[cpu]=regs 後、kdmp_dump_gprs/x86(KDMP_IPI) が status[IPI].x86_regs.gprs/excp_gprs へ保存。
- カスタム NMI CPU: do_nmi で kdmp_nmi_regs[cpu]=regs 後、kdmp_dump_gprs/x86(KDMP_NMI) が status[NMI].x86_regs.gprs/excp_gprs へ保存。
- 標準 vmcore 全CPU: crash_save_cpu(regs, cpu) が elf_core_copy_regs(&prstatus.pr_reg, regs) を使って crash_notes[cpu] の NT_PRSTATUS として保存。

---

## `kexec_should_crash()` の条件詳細

### 関数本体（kernel/crash_core.c:83）

```c
int kexec_should_crash(struct task_struct *p)
{
    if (crash_kexec_post_notifiers)   // ① 最優先チェック
        return 0;
    if (in_interrupt() || !p->pid || is_global_init(p) || panic_on_oops)
        return 1;                     // ② 4条件のいずれかで真
    return 0;
}
```

呼ばれる場所は `oops_end()` の先頭だけ（dumpstack.c:388）。

```c
void oops_end(unsigned long flags, struct pt_regs *regs, int signr)
{
    if (regs && kexec_should_crash(current))
        crash_kexec(regs);   // ← 1 のときここで直接 kexec に飛ぶ
    ...
    if (in_interrupt())   panic("Fatal exception in interrupt");
    if (panic_on_oops)    panic("Fatal exception");
    rewind_stack_and_make_dead(signr);   // 通常プロセスのみ終了
}
```

---

### ① `crash_kexec_post_notifiers` が最優先

panic.c:62 の定義：

```c
bool crash_kexec_post_notifiers =
    IS_ENABLED(CONFIG_CUSTOM_CRASHCUMP) && IS_ENABLED(CONFIG_KEXEC);
```

| CONFIG_CUSTOM_CRASHCUMP | crash_kexec_post_notifiers のデフォルト | kexec_should_crash の戻り値 |
|------------------------|---------------------------------------|---------------------------|
| 有効 | **true** | 常に **0**（oops_end から直接 kexec しない） |
| 無効 | false | ②の4条件で決まる |

`CONFIG_CUSTOM_CRASHCUMP` が有効な場合、`kexec_should_crash()` は常に 0 を返す。
`oops_end` から `crash_kexec(regs)` は呼ばれず、必ず `panic()` → `vpanic()` を経由する。
これにより `panic_dump_gprs()` が確実に実行され、カスタムダンプが動く仕組み。

---

### ② 4つの条件（`crash_kexec_post_notifiers=false` のときのみ評価）

| 条件 | 意味 | 即時 kexec する理由 |
|------|------|-------------------|
| `in_interrupt()` | 割り込みハンドラ内で例外 | 割り込みコンテキストでは `panic()` の中断ロジックが機能しない |
| `!p->pid` | PID 0（idle タスク） | アイドルタスクを終了させることはできない |
| `is_global_init(p)` | PID 1（init プロセス） | init が死ぬとシステムが成立しない |
| `panic_on_oops` | カーネルブートパラメータ | すべての oops を致命的扱いにする設定 |

上記4条件に当てはまらない（通常ユーザープロセスが fault した）場合、`kexec_should_crash()` は 0 を返し、`oops_end` は `rewind_stack_and_make_dead()` でそのプロセスを終了するだけでシステムは継続する。

---

### 2つの経路の違い（vmcore / crash_notes への影響）

```
kexec_should_crash=1（crash_kexec_post_notifiers=false かつ4条件成立）
      ↓
oops_end で crash_kexec(regs) 直接呼出
      ↓
crash_setup_regs(&fixed_regs, regs)  ← 例外時 pt_regs を保存元にする
      ↓
__crash_kexec(&fixed_regs)  → panic/vpanic を経由しない
      └─ panic_dump_gprs() は走らない
         → カスタムダンプ status[PANIC] への書込なし
         → vmcore panic CPU の pr_reg = 例外時 RIP/RSP


kexec_should_crash=0（CONFIG_CUSTOM_CRASHCUMP 有効時のデフォルト）
      ↓
oops_end は crash_kexec しない → panic/vpanic へ
      ↓
panic_dump_gprs() 実行  → カスタムダンプ status[PANIC] に書込
      ↓
__crash_kexec(NULL)
      ↓
crash_setup_regs(&fixed_regs, NULL)  ← 現在コンテキスト（panic 内）を保存元にする
      └─ vmcore panic CPU の pr_reg = vpanic() 内時点の RIP/RSP
```
