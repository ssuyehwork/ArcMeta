1. 关于“.scch”
“.scch”和“mainwindow”窗口没有任何关系
“.scch”仅可应用在“ScanDialog.cpp”，只有在点击“扫描”按钮后加载“.scch”并在打开“ScanDialog.cpp”界面后才可以使用，当关闭“ScanDialog.cpp”界面后同时卸载“.scch”。所以“.scch”缓存文件和主界面没有任何关系，这是为了不占用内存

2. “ScanDialog.cpp”界面必须支持.svg 图片，可以直接参考/调用mainwindow 内容容器显示的svg方式