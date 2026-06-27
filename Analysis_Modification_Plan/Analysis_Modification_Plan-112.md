# 寮傛楂樻€ц兘鎭㈠鏂囦欢澶圭櫥璁拌繘搴︾幆 鈥斺€?implementation_plan.md

## 1. 鐩爣涓庤儗鏅?
鐢变簬涔嬪墠鐨勫悓姝ユ繁搴﹂€掑綊鎵弿涓ラ噸褰卞搷浜嗗唴瀹归潰鏉跨殑杞藉叆鎬ц兘锛屼笂涓€浠ｄ紭鍖栦腑褰诲簳绉婚櫎浜嗘枃浠跺す宸茬櫥璁拌繘搴︼紙`registrationProgress`锛夌殑璁＄畻涓庤祴鍊奸€昏緫銆傝繖瀵艰嚧 `ItemRecord::registrationProgress` 濮嬬粓淇濇寔鍏跺垵濮嬮粯璁ゅ€?`-1.0`锛屼粠鑰岄樆鏂簡缁樺埗钃濊壊鐧惧垎姣斿渾鍦堢殑璺緞锛屼娇鍏堕暱涔呭け鏁堛€?
涓轰簡閲嶆柊鎭㈠杩欏杩涘害鐜粯鍒讹紝涓旂‘淇濆ぇ鏂囦欢澶瑰姞杞戒綋楠屾祦鐣咃紙涓嶅嚭鐜板崱姝绘垨鐣岄潰绌虹櫧锛夛紝鏈柟妗堟彁渚涗竴濂?**鈥滃紓姝ュ崟绾跨▼闃熷垪鎵弿 + 灞€閮ㄥ閲忓埛鏂?+ QDirIterator 楂樻晥杩唬鈥?* 鐨勬儼鎬у姞杞界瓥鐣ャ€?
---

## 2. 鏂规璇︾粏璁捐

### 2.1 鐗╃悊闃插尽锛氬崟绾跨▼姹犻槦鍒?
璁＄畻鏂囦欢澶圭殑閫掑綊宸茬櫥璁版枃浠舵瘮渚嬫秹鍙婄鐩?I/O銆備负闃叉骞跺彂澶氫釜鏂囦欢澶瑰悓鏃惰繘琛屾繁搴﹂€掑綊鎵弿鏃堕€犳垚鏈烘纭洏绛夊瓨鍌ㄨ澶囩殑瀵婚亾纾佸ご鍐茬獊銆佹伓鍖栨暣浣?I/O 鍚炲悙鐜囷紝鏈柟妗堝皢锛?- 鍦?`ContentPanel` 鍐呴儴鍒濆鍖栦竴涓笓灞炵殑 `QThreadPool m_progressThreadPool`锛屽苟寮鸿灏嗗叾 `maxThreadCount` 璁句负 `1`銆?- 杩欐牱锛屾墍鏈夌殑鏂囦欢澶硅繘搴﹁绠椾换鍔″皢浠ユ帓闃熺殑褰㈠紡涓茶鎵ц锛屽畬鍏ㄩ伩鍏嶇鐩樻姠鍗犮€?
### 2.2 璁＄畻閫昏緫鍗囩骇锛歈DirIterator 浠ｆ浛鎵嬪啓閫掑綊

鎵嬪啓 `QDir::entryInfoList` 閫掑綊浼氬洜涓哄疄渚嬪寲杩囧 `QFileInfoList` 瀵艰嚧澶ч噺鐨勫爢鍐呭瓨鍒嗛厤锛屼笖鍦ㄦ繁灞傜洰褰曚笅闈复鐖嗘爤鐨勯殣鎮ｃ€?鎴戜滑浣跨敤搴曞眰鐨?`QDirIterator it(..., QDirIterator::Subdirectories)`锛屽湪瀛愮嚎绋嬩腑浠ユ瀬楂橀€熷害杩涜瀛愭潯鐩祦寮忔灇涓撅紝姣斿父瑙勯€掑綊閫熷害蹇暟鍊嶏紝鏋佸害杞婚噺銆?
---

## 3. 鎷熷仛淇敼鐨勬枃浠?
### 3.1 [MODIFY] [ModelContract.h](file:///g:/C++/ArcMeta/ArcMeta/src/core/ModelContract.h)
纭 `RegistrationProgressRole` 鏄惁宸茬粡瀹氫箟锛堝厛鍓嶅凡楠岃瘉锛屽凡瀛樺湪涓斾负 `Qt::UserRole + 205`锛屾湰鏂囦欢鏃犻渶淇敼锛屼繚鎸佸師鏍凤級銆?
### 3.2 [MODIFY] [ContentPanel.h](file:///g:/C++/ArcMeta/ArcMeta/src/ui/ContentPanel.h)

1. 鍦?`FerrexVirtualDbModel` 绫诲畾涔変腑锛屾毚闇叉洿鏂拌繘搴︾殑鍏叡鎺ュ彛锛?   ```cpp
   /**
    * @brief 2026-06-27 楂樻€ц兘澧為噺鍒锋柊锛氫富绾跨▼鏇存柊鐗瑰畾璺緞鏂囦欢澶圭殑鐧昏杩涘害骞惰Е鍙戝眬閮ㄥ埛鏂?    */
   void updateRegistrationProgress(const QString& path, double progress);
   ```

2. 鍦?`ContentPanel` 绫诲畾涔変腑锛屽姞鍏ュ涓嬬鏈夋垚鍛樺彉閲忓強杈呭姪鍑芥暟锛?   ```cpp
   // 澶存枃浠跺紩鐢?   #include <QThreadPool>
   #include <QMutex>

   // 鍦?ContentPanel 鐨?private 鍖哄煙
   QThreadPool m_progressThreadPool;       // 杩涘害璁＄畻涓撶敤绾跨▼姹?   QSet<QString> m_activeCalculations;     // 褰撳墠姝ｅ湪璁＄畻鐨勮矾寰勯泦鍚?   QMutex m_calcMutex;                     // 鐢ㄤ簬淇濇姢 m_activeCalculations 鐨勪簰鏂ラ攣

   void startAsyncProgressCalculation();   // 鍚姩寮傛璁＄畻浠诲姟
   double calculateFolderProgressInternal(const QString& folderPath); // 搴曞眰鎵弿璁＄畻
   ```

### 3.3 [MODIFY] [ContentPanel.cpp](file:///g:/C++/ArcMeta/ArcMeta/src/ui/ContentPanel.cpp)

#### 1. 鍒濆鍖栦笌鍙嶅垵濮嬪寲

鍦?`ContentPanel::ContentPanel` 鏋勯€犲嚱鏁颁腑鍒濆鍖栫嚎绋嬫睜锛?```cpp
m_progressThreadPool.setMaxThreadCount(1); // 鐗╃悊闃插尽锛氫覆琛?I/O 浠诲姟
```

鍦?`ContentPanel::~ContentPanel` 鏋愭瀯鍑芥暟涓瓑寰呯嚎绋嬪畨鍏ㄧ粨鏉燂細
```cpp
m_progressThreadPool.waitForDone();
```

#### 2. 娣诲姞妯″瀷鏇存柊鎺ュ彛瀹炵幇
```cpp
void FerrexVirtualDbModel::updateRegistrationProgress(const QString& path, double progress) {
    auto it = m_pathToIndex.find(path);
    if (it != m_pathToIndex.end()) {
        int row = it->second;
        m_allRecords[row].registrationProgress = progress;
        // 瑙﹀彂灞€閮ㄩ噸缁?        QModelIndex idx = index(row, 0);
        emit dataChanged(idx, idx, {RegistrationProgressRole});
    }
}
```

#### 3. 寮傛浠诲姟瑙﹀彂涓庤绠楀疄鐜?```cpp
#include <QDirIterator>

void ContentPanel::startAsyncProgressCalculation() {
    int reqId = m_loadRequestId.load();
    std::vector<QString> dirsToCalculate;

    // 涓荤嚎绋嬪彧璇婚亶鍘?    for (const auto& r : m_model->allRecords()) {
        // 浠呭鐞嗙湡瀹炵殑鏂囦欢澶癸紝涓斾箣鍓嶆病鏈夎绠楄繃杩涘害锛堝垵濮嬪€间负 -1.0锛?        if (r.isDir && !r.isCategory && r.registrationProgress < -0.5) {
            dirsToCalculate.push_back(r.path);
        }
    }

    if (dirsToCalculate.empty()) return;

    for (const auto& dirPath : dirsToCalculate) {
        {
            QMutexLocker lock(&m_calcMutex);
            if (m_activeCalculations.contains(dirPath)) continue;
            m_activeCalculations.insert(dirPath);
        }

        // 浣跨敤 QThreadPool 寮傛鎻愪氦
        m_progressThreadPool.start([this, dirPath, reqId]() {
            double progress = calculateFolderProgressInternal(dirPath);

            // 鍥炶皟涓荤嚎绋?            QMetaObject::invokeMethod(this, [this, dirPath, progress, reqId]() {
                // 妫€鏌ヨ姹?ID 鏄惁涓€鑷达紝闃叉鐢变簬鐢ㄦ埛鍒囨崲浜嗘枃浠跺す瀵艰嚧杩囨湡鍥炶皟閿欒鍐欏叆
                if (reqId == m_loadRequestId.load()) {
                    m_model->updateRegistrationProgress(dirPath, progress);
                }
                
                QMutexLocker lock(&m_calcMutex);
                m_activeCalculations.remove(dirPath);
            }, Qt::QueuedConnection);
        });
    }
}

double ContentPanel::calculateFolderProgressInternal(const QString& folderPath) {
    long long totalCount = 0;
    long long managedCount = 0;

    // 楂樻晥搴曞眰鐨勬壂鎻忓櫒锛氫粎鏋氫妇鏂囦欢涓庢枃浠跺す锛屼娇鐢?Subdirectories 閫掑綊
    QDirIterator it(folderPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString childPath = it.next();
        totalCount++;

        // 璁块棶鍏冩暟鎹閿佺紦瀛?        if (MetadataManager::instance().getMeta(childPath.toStdWString()).hasUserOperations()) {
            managedCount++;
        }
    }

    return (totalCount == 0) ? 0.0 : (double)managedCount / totalCount;
}
```

#### 4. 鎸傝浇瑙﹀彂鐐?鍦ㄦ墍鏈夋ā鍨嬪姞杞藉畬姣曞苟璁剧疆璁板綍鍚庯紝瑙﹀彂寮傛浠诲姟锛?- 鍦?`loadDirectory` 瀹屾垚鐨?lambda 鍐咃細
  ```cpp
  // 绾?L2378 琛岋紝璁剧疆瀹屾ā鍨嬭褰曞悗
  m_model->setRecords(allItems);
  m_proxyModel->sort(0);
  startAsyncProgressCalculation(); // 瑙﹀彂寮傛杩涘害鎵弿
  ```
- 鍦?`loadPaths` 瀹屾垚鐨?lambda 鍐咃細
  ```cpp
  // 绾?L2689 琛?  m_model->setRecords(newRecords);
  startAsyncProgressCalculation(); // 瑙﹀彂寮傛杩涘害鎵弿
  ```
- 鍦?`loadCategory` 瀹屾瘯鍚庯細
  ```cpp
  // 绾?L2578 琛?  m_model->setRecords(allRecords);
  m_proxyModel->sort(0);
  startAsyncProgressCalculation(); // 瑙﹀彂寮傛杩涘害鎵弿
  ```

---

## 4. 楠岃瘉璁″垝

1. **缂栬瘧纭**锛氭鏌ラ」鐩湪 CMake 鐜涓嬫棤浠讳綍鏂板鏂囦欢缂栬瘧閿欒銆?2. **鎬ц兘涓庢祦鐣呭害楠岃瘉**锛?   - 鎵撳紑鍖呭惈瓒呰繃 1,000 涓枃浠跺す鐨勭洰褰曪紙渚嬪澶у垎鍖虹殑鏍圭洰褰曪級锛岄獙璇佺晫闈㈡覆鏌撲緷鏃х绾у畬鎴愶紝鏃犱换浣曞崱椤裤€?   - 楠岃瘉鍙充笂瑙掔殑杩涘害寮х嚎浼氬湪鍚庡彴璁＄畻瀹屾瘯鍚庤嚜鐒垛€滄诞鐜扳€濆湪姣忎釜鏂囦欢澶瑰崱鐗囦笂銆?   - 鎮仠鍦ㄨ繘搴﹀姬涓婏紝楠岃瘉 ToolTip 鏄惁鑳芥纭樉寮忔樉绀?`鐧昏杩涘害: XX%`銆?3. **鏁版嵁涓€鑷存€ч獙璇?*锛?   - 鍦ㄦ枃浠跺す闂村垏鎹紝楠岃瘉鍓嶄竴涓枃浠跺す鐨勮绠楅槦鍒楀湪鍒囪蛋鏃惰鎶涘純锛屾柊鏂囦欢澶圭殑璁＄畻閫昏緫鍑嗙‘鎵ц锛岃繘搴︽潯涓嶅嚭鐜版贩涔便€?
