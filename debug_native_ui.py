import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from pathlib import Path
from PyQt5.QtWidgets import QApplication
from app.services.settings_service import Settings
from app.ui.main_window import MainWindow

code = '''#include <stdio.h>

int main() {
    int n = 10;
    int sum = 0;

    for (int i = 1; i <= n; i++) {
        sum += i;
    }

    printf("Sum of 1..10 = %d\n", sum);

    if (sum > 50) {
        printf("That is a big sum!\n");
    } else {
        printf("That is a small sum.\n");
    }

    return 0;
}
'''

app = QApplication([])
w = MainWindow(app, Settings())
# use a temp C file as the current source
path = Path('Temp/debug_native.c')
path.parent.mkdir(exist_ok=True)
path.write_text(code, encoding='utf-8')
w._load_file(path)
print('language', w._session.language)
print('phase results', [(r.phase.id, r.status, r.error) for r in w._session.phase_results])
print('artifacts', sorted(w._session.artifacts.keys()))
print('token count', len(w._session.artifacts.get('token_stream', {}).get('tokens', [])))
print('cst top items', w.cst_view.topLevelItemCount())
print('ast top items', w.ast_view._tree.topLevelItemCount())
print('semantic summary', w.semantic_view._summary.text())
print('ir summary', w.ir_view._summary.text())
print('opt summary', w.optimization_view._summary.text())
print('asm summary', w.assembly_view._summary.text())
