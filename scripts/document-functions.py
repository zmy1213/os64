#!/usr/bin/env python3
"""Document explicit C++ definitions from Clang's AST; does not draw images.

Run from any directory with python3 scripts/document-functions.py. Assembly is
tracked separately in the illustrated function index. Source hashes bind each
entry to its actual definition, including newly added working-tree programs.
"""
from pathlib import Path
import subprocess,json,hashlib
root=Path(__file__).resolve().parents[1]
files=sorted([*root.glob('kernel/**/*.cpp'),*root.glob('kernel/**/*.hpp'),*root.glob('user/**/*.cpp'),*root.glob('user/**/*.hpp')])
functions=[];errors=[]
for p in files:
 data=p.read_bytes();relative=str(p.relative_to(root));include=root/('kernel' if relative.startswith('kernel/') else 'user')
 cmd=['clang++','--target=x86_64-elf','-std=c++17','-x','c++','-ffreestanding','-fno-exceptions','-fno-rtti','-mgeneral-regs-only','-I',str(include),'-Xclang','-ast-dump=json','-fsyntax-only',str(p)]
 result=subprocess.run(cmd,capture_output=True)
 if result.returncode:
  errors.append({'path':relative,'diagnostic':result.stderr.decode()});continue
 ast=json.loads(result.stdout)
 def walk(node,scope=()):
  kind=node.get('kind');name=node.get('name','');nested=scope
  if kind in ('NamespaceDecl','CXXRecordDecl') and name and not node.get('isImplicit'):nested=(*scope,name)
  if kind in ('FunctionDecl','CXXMethodDecl','CXXConstructorDecl','CXXDestructorDecl','CXXConversionDecl') and not node.get('isImplicit'):
   body=next((x for x in node.get('inner',[]) if x.get('kind')=='CompoundStmt'),None)
   loc=node.get('loc',{});offset=loc.get('offset')
   if body and not loc.get('includedFrom') and offset is not None:
    token=data[offset:offset+loc.get('tokLen',len(name))].decode(errors='ignore')
    if token and (name.startswith(token) or token.startswith(name)):
     span=node.get('range',{});start=span.get('begin',{}).get('offset');end=span.get('end',{}).get('offset')
     if start is not None and end is not None and end<len(data):
      line=data[:offset].count(b'\n')+1
      functions.append({'path':relative,'name':name,'qualified_name':'::'.join((*scope,name)),'line':line,'end_line':data[:end].count(b'\n')+1,'signature':node.get('type',{}).get('qualType',''),'source_sha256':hashlib.sha256(data[start:end+span['end'].get('tokLen',1)]).hexdigest()})
  for child in node.get('inner',[]):walk(child,nested)
 walk(ast)
if errors:
 for error in errors:
  print(error['path'],error['diagnostic'])
 raise SystemExit('Clang could not parse every file; the existing inventory was preserved.')
functions=sorted({(x['path'],x['line'],x['name']):x for x in functions}.values(),key=lambda x:(x['path'],x['line']))
out=root/'docs/illustrated/FUNCTION_INVENTORY.json';out.parent.mkdir(exist_ok=True)
out.write_text(json.dumps({'working_tree_source_changes':subprocess.check_output(['git','status','--porcelain','--','kernel','user'],cwd=root,text=True).splitlines(),'source_files':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in files},'method':'Clang AST definitions; kernel/user .cpp/.hpp; implicit functions and declarations excluded; assembly entries documented separately','git_revision':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'file_count':len(files),'definition_count':len(functions),'parse_errors':errors,'functions':functions},ensure_ascii=False,indent=2)+'\n')
print('scanned',len(files),'files;',len(functions),'function definitions;',len(errors),'parse failures',flush=True)
from collections import Counter
print(Counter('/'.join(x['path'].split('/')[:2]) for x in functions),flush=True)
for e in errors: print(e['path'],e['diagnostic'][:150])
