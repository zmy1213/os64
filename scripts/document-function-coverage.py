#!/usr/bin/env python3
"""Resolve explicit C++ definitions to tutorial entries and shared image panels.

Run from any working directory with python3 scripts/document-function-coverage.py.
The default audits without writing; add --write to refresh Markdown and JSON.
This reads the frozen function inventory and validates every full source-file
hash. It does not compile code or generate/edit any image.
"""
from pathlib import Path
from collections import Counter
import json
import re
import unicodedata
import hashlib
import sys

ROOT = Path(__file__).resolve().parents[1]
DOCS = ROOT / 'docs/illustrated'

DOCUMENTS = {
    'SMP_BOOT_FUNCTIONS.md': [
        'kernel/cpu/smp.cpp', 'kernel/cpu/topology.cpp', 'kernel/cpu/cpu.cpp',
        'kernel/cpu/xapic.hpp', 'kernel/interrupts/interrupts.cpp',
    ],
    'SCHEDULER_FUNCTIONS.md': ['kernel/task/scheduler.cpp'],
    'COOPERATION_FUNCTIONS.md': [
        'kernel/fs/fd.cpp', 'kernel/net/network.cpp', 'user/programs/coop_test.cpp',
        'user/programs/smp_test.cpp', 'user/programs/udp_mixed.cpp', 'user/bench_workload.hpp',
    ],
    'PARALLEL_REDUCTION.md': ['user/programs/parallel_reduce.cpp'],
    'DEVICES_IO_FUNCTIONS.md': [
        'kernel/console/console.cpp', 'kernel/device/pci.cpp',
        'kernel/interrupts/keyboard.cpp', 'kernel/interrupts/pic.cpp',
        'kernel/interrupts/pit.cpp', 'kernel/interrupts/serial.cpp',
        'kernel/log/log.cpp', 'kernel/storage/ata_pio.cpp',
        'kernel/storage/block_device.cpp', 'kernel/storage/boot_volume.cpp',
        'kernel/runtime/runtime.cpp', 'kernel/net/network.hpp',
        'kernel/net/network_irq.cpp', 'kernel/net/virtio_net.cpp', 'kernel/perf/perf.cpp',
    ],
    'SYSTEM_MEMORY_FUNCTIONS.md': [
        'kernel/core/kernel_main.cpp', 'kernel/memory/page_allocator.cpp',
        'kernel/memory/paging.cpp', 'kernel/memory/address_space.cpp',
        'kernel/memory/heap.cpp', 'kernel/memory/kmemory.cpp', 'kernel/memory/kmemory.hpp',
    ],
    'USER_ABI_FUNCTIONS.md': [
        'kernel/syscall/syscall.cpp', 'kernel/task/elf_loader.cpp',
        'user/memory.cpp', 'user/os64.hpp', 'user/smp.hpp', 'user/udp.hpp',
    ],
    'FILESYSTEM_FUNCTIONS.md': [
        'kernel/fs/os64fs.cpp', 'kernel/fs/directory.cpp',
        'kernel/fs/file.cpp', 'kernel/fs/vfs.cpp',
    ],
    'SHELL_FUNCTIONS.md': ['kernel/shell/parser.cpp', 'kernel/shell/shell.cpp'],
    'USER_PROGRAM_FUNCTIONS.md': [
        'user/programs/' + name + '.cpp' for name in [
            'badptr', 'bench', 'bench_ipc', 'cat', 'echo', 'edit', 'false', 'fault',
            'fp_test', 'fs_test', 'hello', 'ls', 'mem_test', 'mkdir', 'nxfault',
            'perf_test', 'pipe_test', 'pwd', 'rm', 'sched_test', 'sleep',
            'spawn_test', 'spin', 'stackfault', 'sync', 'true', 'ud2', 'udp_test',
            'wc', 'writer',
        ]
    ],
}

FIGURES = {
    'SMP_BOOT_FUNCTIONS.md': {
        'B': 'images/smp-boot.png', 'G': 'images/kernel-gate.png', 'T': 'images/topology.png',
    },
    'SCHEDULER_FUNCTIONS.md': {
        'R': 'images/ready-queue.png', 'C': 'images/context-switch.png',
        'W': 'images/sleep-wakeup.png', 'P': 'images/process-reaping.png',
    },
    'COOPERATION_FUNCTIONS.md': {
        'P': 'images/pipe-cooperation.png', 'U': 'images/udp-wait.png',
        'M': 'images/parallel-workers.png',
    },
    'PARALLEL_REDUCTION.md': {'A': 'images/parallel-reduction.png'},
    'DEVICES_IO_FUNCTIONS.md': {
        'I': 'images/console-input.png', 'H': 'images/interrupt-devices.png',
        'S': 'images/storage-io.png', 'V': 'images/virtio-dma.png',
        'O': 'images/logging-observability.png',
    },
    'SYSTEM_MEMORY_FUNCTIONS.md': {
        'K': 'images/kernel-bringup.png', 'A': 'images/physical-pages.png',
        'V': 'images/paging-address-space.png', 'H': 'images/heap-memory.png',
        'S': 'images/syscall-boundary.png', 'U': 'images/elf-user-abi.png',
    },
    'USER_ABI_FUNCTIONS.md': {
        'S': 'images/syscall-boundary.png', 'U': 'images/elf-user-abi.png',
        'H': 'images/heap-memory.png',
    },
    'FILESYSTEM_FUNCTIONS.md': {
        'L': 'images/os64fs-layout.png', 'T': 'images/filesystem-transactions.png',
        'V': 'images/vfs-paths.png',
    },
    'SHELL_FUNCTIONS.md': {
        'Q': 'images/shell-parser.png', 'X': 'images/shell-pipeline.png',
        'I': 'images/console-input.png', 'H': 'images/interrupt-devices.png',
        'O': 'images/logging-observability.png', 'L': 'images/os64fs-layout.png',
        'T': 'images/filesystem-transactions.png', 'V': 'images/vfs-paths.png',
    },
    'USER_PROGRAM_FUNCTIONS.md': {'U': 'images/user-tools.png', 'E': 'images/user-editor.png'},
}

def slug(heading):
    heading = heading.lower()
    heading = ''.join(c for c in heading if c in '-_ ' or unicodedata.category(c)[0] in 'LNM')
    return heading.replace(' ', '-')

def plain_heading(heading):
    return heading.replace('`', '')

def load_doc(name):
    path = DOCS / name
    if not path.exists():
        return None
    lines = path.read_text().splitlines()
    headings = []
    used = Counter()
    current = None
    sections = []
    for number, line in enumerate(lines, 1):
        match = re.match(r'^(#{1,6}) (.+)$', line)
        if match:
            anchor = slug(match[2])
            if used[anchor]: anchor += '-' + str(used[anchor])
            used[slug(match[2])] += 1
            current = {'line': number, 'text': match[2], 'anchor': anchor, 'level': len(match[1])}
            headings.append(current)
        sections.append(current)
    return {'lines': lines, 'headings': headings, 'sections': sections}

def function_token_matches(token, fn):
    token = token.strip().split('(')[0].strip()
    name = fn['name']
    if '::' in fn['qualified_name'] and fn['qualified_name'].startswith('LocalIrqGuard::'):
        return token == fn['qualified_name']
    prefix = {
        'user/programs/coop_test.cpp': 'coop_test::',
        'user/programs/smp_test.cpp': 'smp_test::',
        'user/programs/udp_mixed.cpp': 'udp_mixed::',
        'user/bench_workload.hpp': 'bench_workload::',
        'user/programs/parallel_reduce.cpp': 'parallel_reduce::',
        'kernel/console/console.cpp': 'console::',
        'kernel/device/pci.cpp': 'pci::',
        'kernel/interrupts/keyboard.cpp': 'keyboard::',
        'kernel/interrupts/pic.cpp': 'pic::',
        'kernel/interrupts/pit.cpp': 'pit::',
        'kernel/interrupts/serial.cpp': 'serial::',
        'kernel/log/log.cpp': 'log::',
        'kernel/storage/ata_pio.cpp': 'ata_pio::',
        'kernel/storage/block_device.cpp': 'block_device::',
        'kernel/storage/boot_volume.cpp': 'boot_volume::',
        'kernel/runtime/runtime.cpp': 'runtime::',
        'kernel/net/network.hpp': 'network::',
        'kernel/net/network_irq.cpp': 'network_irq::',
        'kernel/net/virtio_net.cpp': 'virtio_net::',
        'kernel/perf/perf.cpp': 'perf::',
    }.get(fn['path'], '')
    target = fn['qualified_name'] if '::' in fn['qualified_name'] else name
    return token == (prefix + target) or (not prefix and token == name) or (
        fn['path'] == 'user/programs/parallel_reduce.cpp' and token == name
    )

def normalize_blocks(text):
    # Expand B.a, B(a)(b), and U(f,e); prefixes are resolved per tutorial.
    blocks = []
    for match in re.finditer(r'\b([A-Z])(?:\.([a-f])|((?:\([a-f](?:[,、][a-f]|[-–—…][a-f]|\.{2,3}[a-f])*\))+))', text):
        prefix = match[1]
        letters = [match[2]] if match[2] else []
        if not match[2]:
            for group in re.findall(r'\(([^)]+)\)', match[3]):
                interval = re.fullmatch(r'([a-f])(?:[-–—…]|\.{2,3})([a-f])', group)
                if interval:
                    letters.extend(chr(x) for x in range(ord(interval[1]), ord(interval[2])+1))
                else:
                    letters.extend(re.findall(r'[a-f]', group))
        blocks.extend(prefix + '(' + letter + ')' for letter in letters)
    return list(dict.fromkeys(blocks))

def find_explanation(doc, fn):
    lines = doc['lines']
    # An exact source file + definition line distinguishes same-named functions
    # and overloaded operators. A heading body or a single table row is required.
    for number, line in enumerate(lines, 1):
        source_links = re.findall(r'\]\(([^)]+)#L(\d+)\)', line)
        if not any(path.endswith('/' + fn['path']) and int(start)==fn['line']
                   for path,start in source_links):
            continue
        head = doc['sections'][number - 1]
        if not head:
            continue
        if line.startswith('|'):
            return head, number, normalize_blocks(line), 'table_row'
        if re.match(r'^#{2,6} ', lines[head['line']-1]) and '`' in head['text']:
            end = next((h['line']-1 for h in doc['headings'] if h['line']>head['line']), len(lines))
            body = '\n'.join(lines[head['line']-1:end])
            return head, head['line'], normalize_blocks(body), 'heading'
    # Independent function rows take precedence over grouped function headings.
    for number, line in enumerate(lines, 1):
        if not line.startswith('|'): continue
        cells = line.split('|')
        first = cells[1] if len(cells) > 1 else ''
        if any(function_token_matches(token, fn) for token in re.findall(r'`([^`]+)`', first)):
            head = doc['sections'][number - 1]
            blocks = normalize_blocks(line)
            if not blocks: blocks = normalize_blocks(head['text'])
            return head, number, blocks, 'table_row'
    # A specific function heading is required; passing prose mentions do not count.
    for number, line in enumerate(lines, 1):
        if not re.match(r'^#{2,6} ', line): continue
        if any(function_token_matches(token, fn) for token in re.findall(r'`([^`]+)`', line)):
            head = doc['sections'][number - 1]
            end = next((h['line'] - 1 for h in doc['headings'] if h['line'] > number), len(lines))
            body = '\n'.join(lines[number - 1:end])
            return head, number, normalize_blocks(body), 'heading'
    return None

def figure_records(doc_name, blocks):
    figures = []
    for block in blocks:
        prefix = block[0]
        image = FIGURES.get(doc_name, {}).get(prefix)
        if not image: continue
        existing = next((r for r in figures if r['path'] == image), None)
        if existing:
            existing['blocks'].append(block)
        else:
            figures.append({'path': image, 'blocks': [block], 'available': (DOCS / image).is_file()})
    return figures

def main():
    inventory_path = DOCS / 'FUNCTION_INVENTORY.json'
    inventory = json.loads(inventory_path.read_text())
    source_errors = [path for path, digest in inventory['source_files'].items()
                     if hashlib.sha256((ROOT/path).read_bytes()).hexdigest() != digest]
    if source_errors:
        raise RuntimeError('Frozen source-file hashes do not match: ' + ', '.join(source_errors))
    docs = {name: load_doc(name) for name in DOCUMENTS}
    by_path = {path: name for name, paths in DOCUMENTS.items() for path in paths}
    results = []
    gaps = []
    for fn in inventory['functions']:
        name = by_path.get(fn['path'])
        explanation = find_explanation(docs[name], fn) if name and docs[name] else None
        record = {
            'path': fn['path'], 'name': fn['name'], 'qualified_name': fn['qualified_name'],
            'line': fn['line'], 'end_line': fn['end_line'], 'signature': fn['signature'],
            'source_sha256': fn['source_sha256'],
            'source_link': '../../' + fn['path'] + '#L' + str(fn['line']),
            'status': 'explained' if explanation else 'pending',
            'tutorial': None, 'figures': [],
        }
        if explanation:
            head, line, blocks, location_kind = explanation
            if not blocks:
                raise RuntimeError('No diagram block for ' + fn['path'] + ':' + str(fn['line']))
            unknown = [b for b in blocks if b[0] not in FIGURES.get(name,{})]
            if unknown:
                raise RuntimeError('Unknown figure blocks in ' + name + ': ' + ','.join(unknown))
            record['tutorial'] = {
                'path': name, 'link': name + '#' + head['anchor'],
                'heading': plain_heading(head['text']), 'heading_line': head['line'],
                'function_line': line, 'location_kind': location_kind,
                'figure_blocks': blocks,
            }
            record['figures'] = figure_records(name, blocks)
            record['illustration_status'] = ('available' if all(f['available'] for f in record['figures'])
                                            else 'pending_images')
        elif name:
            gaps.append({'path': fn['path'], 'name': fn['name'], 'line': fn['line'], 'expected_doc': name})
        results.append(record)
    print(json.dumps({'definitions':len(results),'explained':sum(r['status']=='explained' for r in results),'gaps':gaps},ensure_ascii=False))
    if '--write' not in sys.argv:
        return
    if gaps:
        raise RuntimeError('Selected tutorial scope contains unexplained definitions')
    if not any(f['path']=='user/programs/parallel_reduce.cpp' for f in inventory['functions']):
        raise RuntimeError('Inventory must be rescanned with parallel_reduce.cpp before publishing')
    if not docs['PARALLEL_REDUCTION.md']:
        raise RuntimeError('Parallel reduction tutorial must exist before publishing')
    counts = Counter(r['status'] for r in results)
    illustration_counts = Counter(r.get('illustration_status','unmapped') for r in results)
    referenced_images = sorted({f['path'] for r in results for f in r['figures']})
    unavailable_images = [p for p in referenced_images if not (DOCS/p).is_file()]
    file_groups = {}
    for r in results:
        file_groups.setdefault(r['path'], []).append(r)
    coverage = {
        'schema_version':1,
        'inventory':'FUNCTION_INVENTORY.json',
        'inventory_sha256':hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
        'inventory_git_revision':inventory.get('git_revision'),
        'method':'One record per explicit C++ definition from the Clang AST inventory. Explanation coverage requires an actual per-function Markdown heading or table row, not a passing mention. Images are shared process diagrams; figure blocks are resolved within each tutorial. Assembly is separate and not part of C++ counts.',
        'counts':{'cpp_definitions':len(results),'explained':counts['explained'],'pending':counts['pending'],
                  'files_with_definitions':len(file_groups), 'inventoried_source_files':inventory['file_count'],
                  'definitions_with_available_figures':illustration_counts['available'],
                  'definitions_waiting_for_images':illustration_counts['pending_images'],
                  'referenced_images':len(referenced_images),
                  'available_images':len(referenced_images)-len(unavailable_images)},
        'unavailable_images':unavailable_images,
        'tutorial_scopes':DOCUMENTS,
        'functions':results,
    }
    md = ['# 全函数图解索引', '',
        (f"当前库存的 **{len(results)} 个 C++ 函数定义已全部逐函数讲解**。" if not counts['pending'] else
         f"当前库存有 **{len(results)} 个 C++ 函数定义**；已逐函数讲解 **{counts['explained']} 个**，其余 **{counts['pending']} 个待图解**。") +
        "构造/析构和头文件中的显式定义计入；声明、隐式生成函数与汇编不混入总数。", '',
        f"图块共引用 **{len(referenced_images)} 张机制图**，当前已有 **{len(referenced_images)-len(unavailable_images)} 张**；{illustration_counts['pending_images']} 个函数的文字已完成但所关联图片尚未全部到位。配图缺失单独标明，不计为已经完成图片。" if unavailable_images else f"所有函数的图块都已关联到实际文件，共 **{len(referenced_images)} 张机制图**；源码行、讲义标题/表行和图块均在覆盖记录中逐项保存。", '',
        '一张六栏图解释一组协作关系，每个已讲解函数链接到真实标题或表格所在小节；表行定位另保存在 [覆盖记录](FUNCTION_COVERAGE.json)。源码链接使用仓库相对路径与定义起始行。此索引列出实现函数，不将工作窃取、无锁调度、运行时迁移等未来计划标成已实现。', '',
        '库存和提取方法见 [FUNCTION_INVENTORY.json](FUNCTION_INVENTORY.json)。讲义中的同名图块属于各自章节，例如调度 P 指进程回收，协作 P 指管道。', '',
        '| 讲义 | 函数数 | 实际源码范围 |', '| --- | ---: | --- |']
    for doc_name, paths in DOCUMENTS.items():
        count = sum(r['status']=='explained' and r['path'] in paths for r in results)
        md.append(f"| [{doc_name}]({doc_name}) | {count} | " + '、'.join('`'+p+'`' for p in paths) + ' |')
    md += ['', '## 按源码文件查找', '']
    for path, rows in sorted(file_groups.items()):
        explained = sum(r['status']=='explained' for r in rows)
        md += ['### ' + path, '',f'{len(rows)} 个定义；已讲解 {explained} 个。', '',
            '| 函数 | 源码起始行 | 讲义定位 | 图块 |', '| --- | ---: | --- | --- |']
        for r in rows:
            label = r['qualified_name'].replace('|','\\|')
            source = f"[L{r['line']}]({r['source_link']})"
            if r['tutorial']:
                t = r['tutorial']
                detail = '[讲解](' + t['link'] + ')'
                if t['location_kind']=='table_row': detail += '（讲义第'+str(t['function_line'])+'行）'
                parts = []
                for figure in r['figures']:
                    suffix = '' if figure['available'] else '（配图待生成）'
                    parts.append('['+'、'.join(figure['blocks'])+']('+figure['path']+')'+suffix)
                blocks = '；'.join(parts) or '见讲义对应小节'
            else:
                detail, blocks = '待图解', '—'
            md.append('| `'+label+'` | '+source+' | '+detail+' | '+blocks+' |')
        md.append('')
    # Existing tutorials explain these real callable/transition entries and copied user fixtures.
    assembly = [
        ('kernel/cpu/ap_start.asm','smp_trampoline_start',8,'SMP_BOOT_FUNCTIONS.md','ap_start.asm','B(c)'),
        ('kernel/cpu/ap_start.asm','ap_protected',21,'SMP_BOOT_FUNCTIONS.md','ap_start.asm','B(c)'),
        ('kernel/cpu/ap_start.asm','ap_long',40,'SMP_BOOT_FUNCTIONS.md','ap_start.asm','B(c)'),
        ('kernel/cpu/ap_start.asm','.halt',51,'SMP_BOOT_FUNCTIONS.md','ap_start.asm','B(c)'),
        ('kernel/task/context_switch.asm','scheduler_switch_context',29,'SCHEDULER_FUNCTIONS.md','scheduler_switch_context','C(c)'),
        ('kernel/task/context_switch.asm','scheduler_switch_context_and_root',65,'SCHEDULER_FUNCTIONS.md','scheduler_switch_context_and_root','C(c)、C(f)'),
        ('kernel/task/context_switch.asm','user_mode_enter',137,'SCHEDULER_FUNCTIONS.md','user_mode_enter','C(d)、C(e)'),
        ('kernel/task/context_switch.asm','user_mode_resume_kernel',181,'SCHEDULER_FUNCTIONS.md','user_mode_resume_kernel','C(d)、P(b)'),
        ('kernel/task/context_switch.asm','user_mode_smoke_program_start … user_mode_smoke_program_end',214,'SCHEDULER_FUNCTIONS.md','user_mode_smoke_program_start','C(d)、P(a)、P(b)'),
        ('kernel/task/context_switch.asm','user_mode_yield_program_start … user_mode_yield_program_end',365,'SCHEDULER_FUNCTIONS.md','user_mode_yield_program_start','C(c)、W(a)、W(d)'),
    ]
    md += ['## 汇编入口与自检区间（不计入 C++ 总数）', '',
        '下面列出已有逐入口解释的启动桥、切换入口与用户自检代码区间。内部数据标签、区间末尾标签和宏展开出的中断 stub 不作为普通 C++ 函数计数；其它启动/入口汇编仍待后续逐入口图解。', '',
        '| 源码入口或区间 | 源码行 | 讲义 | 图块 |', '| --- | ---: | --- | --- |']
    assembly_records=[]
    for path,name,line,doc_name,needle,blocks in assembly:
        heading=next(h for h in docs[doc_name]['headings'] if needle in h['text'])
        link=doc_name+'#'+heading['anchor']
        source='../../'+path+'#L'+str(line)
        md.append(f'| `{name}` | [{path}:{line}]({source}) | [讲解]({link}) | {blocks} |')
        assembly_records.append({'path':path,'name':name,'line':line,'source_link':source,'tutorial_link':link,'figure_blocks':blocks,'status':'explained','counts_as_cpp_function':False})
    coverage['assembly_examples']=assembly_records
    (DOCS/'FUNCTION_COVERAGE.json').write_text(json.dumps(coverage,ensure_ascii=False,indent=2)+'\n')
    (DOCS/'FUNCTION_INDEX.md').write_text('\n'.join(md)+'\n')
    print('Wrote FUNCTION_INDEX.md and FUNCTION_COVERAGE.json')

if __name__=='__main__':main()
