from pathlib import Path
from app.application.compile_session import CompileSession
from app.application.pipeline import Pipeline
from app.infrastructure.tool_detector import detect_toolchain
from app.infrastructure.backend_runner import BackendRunner
from app.infrastructure.artifact_store import ArtifactStore
from app.application.orchestrator import Orchestrator

root = Path('.')
toolchain = detect_toolchain(root)
backend = BackendRunner(toolchain, cwd=root)
store = ArtifactStore(output_dir=Path('Temp'), cache_dir=Path('Temp/cache'))
session = CompileSession(source_path=Path('examples/study/hello.mc'), language='mini-c', mode='study')
orch = Orchestrator(pipeline=Pipeline(), runner=backend, store=store, toolchain=toolchain)
print('backend available', backend.available(), backend.executable)
orch.compile_all(session)
print('phase statuses', [(r.phase.id, r.status, r.error) for r in session.phase_results])
print('artifact keys', list(session.artifacts.keys()))
for k in ['token_stream','parse_tree','ast','semantic','ir','optimization','assembly','execution']:
    print(k, k in session.artifacts)
