import os
os.environ['QT_QPA_PLATFORM'] = 'offscreen'
from PyQt5.QtWidgets import QApplication
from app.services.settings_service import Settings
from app.ui.main_window import MainWindow

app = QApplication([])
w = MainWindow(app, Settings())
w.compile(silent=True)
print('session artifact keys', sorted(list(w._session.artifacts.keys())))
print('token count', len(w._session.artifacts.get('token_stream', {}).get('tokens', [])))
print('parse present', 'parse_tree' in w._session.artifacts)
print('ast present', 'ast' in w._session.artifacts)
print('semantic present', 'semantic' in w._session.artifacts)
print('ir present', 'ir' in w._session.artifacts)
print('opt present', 'optimization' in w._session.artifacts)
print('assembly present', 'assembly' in w._session.artifacts)
print('cst items', w.cst_view.topLevelItemCount())
print('ast root items', w.ast_view._tree.topLevelItemCount())
print('semantic symbols rows', w.semantic_view._symbols_table.rowCount())
print('semantic diag items', w.semantic_view._diag_tree.topLevelItemCount())
print('ir rows', w.ir_view._table.rowCount())
print('opt items', w.optimization_view._pass_tree.topLevelItemCount())
print('assembly text len', len(w.assembly_view._listing.toPlainText()))
