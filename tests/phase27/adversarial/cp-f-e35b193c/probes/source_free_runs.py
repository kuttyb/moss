import hashlib,json,os,pathlib,subprocess,sys
root=pathlib.Path.cwd();work=root/'tmp/phase27-3/cp-f-agent2-e35b193c';bins={'phase27-2':root.parent/'cp-f-agent2-baseline-e35b193c/build/compiler/moss-dev','phase27-3':root/'build/compiler/moss-dev'};rows=[]
for rev,mode in (('phase27-2','off'),('phase27-2','shadow'),('phase27-3','off'),('phase27-3','shadow')):
 env=os.environ.copy();env.update({'MOSS':str(bins[rev]),'MOSS_EST':mode});cmd=['python3','sdk/advanced-data-acceptance/check_source_free.py','--abi','v9','--profile','debug'];p=subprocess.run(cmd,cwd=root,env=env,text=True,capture_output=True)
 row={'revision':rev,'mode':mode,'compiler_sha256':hashlib.sha256(bins[rev].read_bytes()).hexdigest(),'command':cmd,'returncode':p.returncode,'stdout':p.stdout,'stderr':p.stderr,'ok':p.returncode==0};rows.append(row);print(rev,mode,'rc',p.returncode,p.stdout.strip(),flush=True)
 (work/f'source-free-{rev}-{mode}.json').write_text(json.dumps(row,indent=2,sort_keys=True)+'\n')
 if p.returncode:sys.exit(p.returncode)
fixture=root/'sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss';meta={'fixture':'sdk/advanced-data-acceptance/fixtures/source_free_consumer.moss','fixture_sha256':hashlib.sha256(fixture.read_bytes()).hexdigest(),'source_sha':'e35b193c4f8ffca14fd820f6c938f53e1c1a0744','classification':'BENIGN','runs':rows};(work/'source_free_package_reverification.json').write_text(json.dumps(meta,indent=2,sort_keys=True)+'\n')
