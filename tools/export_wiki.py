"""Export reviewed bilingual pages to a separately cloned GitHub Wiki. Never commits/pushes."""
from pathlib import Path
import argparse,re
ROOT=Path(__file__).resolve().parents[1]
def rewrite(text,repository,branch):
 def link(m):
  label,url=m.groups()
  if url.startswith('../'):
   file=url[3:];base='https://raw.githubusercontent.com/' if file.startswith('assets/') else 'https://github.com/'
   target=(base+repository+'/'+branch+'/'+file) if file.startswith('assets/') else (base+repository+'/blob/'+branch+'/'+file)
  elif url.endswith('.md') and '/' not in url:target=url[:-3]
  else:target=url
  return '['+label+']('+target+')'
 return re.sub(r'\[([^\]]*)\]\(([^)]+)\)',link,text)
def main():
 p=argparse.ArgumentParser(description=__doc__);p.add_argument('--repository',required=True);p.add_argument('--branch',default='main');p.add_argument('--destination',type=Path,required=True);a=p.parse_args()
 if not re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',a.repository):p.error('Expected OWNER/REPOSITORY')
 target=a.destination.resolve()
 if not (target/'.git').exists():p.error('Destination must be the separately cloned Wiki repository')
 if target==ROOT or target==ROOT/'wiki':p.error('Do not overwrite the source repository')
 for file in (ROOT/'wiki').glob('*.md'):(target/file.name).write_text(rewrite(file.read_text(encoding='utf8'),a.repository,a.branch),encoding='utf8',newline='\n')
 print('Wiki files exported. Inspect git diff before committing and pushing.')
if __name__=='__main__':main()
