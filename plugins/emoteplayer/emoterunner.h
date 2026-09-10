#pragma once

#include <vector>
#include <list>
#include <memory>

#include "emotefile.h"

namespace emoteplayer
{
    class emotenoderef;
    class emotemotionref;
    class emoteengine;

    struct emotelimit // 区域限制
    {
        float originX = 0.0f;
        float originY = 0.0f;
        float width = 0.0f;
        float height = 0.0f;
        float zMax = 0.0f;
        // 实际渲染视口尺寸（可能与 width/height 不同）。
        // shape 判定区域收集时的 NDC→像素换算必须使用实际视口尺寸，
        // 否则判定区域与画面错位。0 表示回退到 width/height。
        float viewW = 0.0f;
        float viewH = 0.0f;
    };
    // shape类型(pointed从PSB node的"shape"字段, 或从frame->src推断)
    // 0=point 1=circle 2=rect(默认) 3=quad
    struct emoterect
    {
        std::string label = "";
        float left = 0;
        float top = 0;
        float width = 0;
        float height = 0;
        int shapeType = 2; // 默认rect
        // shape 判定区域的变换后四角顶点（设备像素，顺时针）。
        // 矩形 shape 经节点全链变换（旋转/缩放/剪切）后是任意四边形，
        // 用包围盒判定会把区域外扩而误命中相邻区域，故保存实际四角
        // 供 pointInEmoteShape 做精确四边形判定；circle/point 仍用
        // left/top/width/height。
        float quad[4][2] = {{0}};
    };
    struct emoteRender // 渲染方式
    {

        int type = 0; // 0:不绘制 1:icon参与网格变形和矩阵变换 2:icon只参与矩阵变换 3:layout/motion变形(无法使用网格和透明度)
        float controlPts[32] = {0.0};
        float opa = 1.0;

        bool hasStencil = false;
        std::vector<emotenoderef*> layerNode;

        // 基础参数
        float originX = 0;
        float originY = 0;
        float width = 0;
        float height = 0;
        // 计算参数  lastMat = attachMat * calcMat
        glm::mat4 attachMat = glm::mat4(1.0f);
        float currCoordx = 0, currCoordy = 0, currCoordz = 0;
        float currAngle = 0, currZx = 1.0, currZy = 1.0;
        float currSx = 0, currSy = 0;
        float currOx = 0, currOy = 0;
        uint32_t currInheritMask = 0x20007FC;
        // 辅助信息
        std::string label;
    };

    // 引用自绘制 - 每次绘制创建独立ref，持有node引用+独立状态，避免多次绘制状态重复
    class emotenoderef
    {
    public:
        emotenoderef(emotenode* en, emoteengine* ee, emotemotionref* em) : currentNode(en), refTop(ee), refMtn(em) {};

        // method
        void checkDrawStatus(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
        void progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
        // 通过 core/render 的 2D 渲染抽象绘制（插件无渲染后端区分）
        void draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget);
        float getCurrentRenderZ();
        const std::vector<emoterect>& getShapeList() const { return shapeList; }

        // ref
        emotenode* currentNode = nullptr;
        emotemotion* currentMtn = nullptr;
        emotemotionref* currentMtnRef = nullptr;
        emoteengine* refTop = nullptr;
        emotemotionref* refMtn = nullptr;

        // render method
        std::vector<emoteRender> renderMethod;

        // shape rect (独立于node的shapeList，每次绘制独立)
        std::vector<emoterect> shapeList;

        // check
        emoteicon* ic = nullptr;
        float originX = 0;
        float originY = 0;
        float width = 0;
        float height = 0;
        bool isNeedDraw = false;
        bool isIcon = false;
        bool isLayout = false;
        // shape 判定层（触摸判定用）：src 以 "shape/" 开头，
        // 或 src 缺省（部分导出的 PSB 剥离了 shape 帧的 src 字段，
        // 解析后落到 layout）且 node type==1
        bool isShape = false;
        emoteframe* frame = nullptr;
        emoteframe* nextframe = nullptr;

        // runtime (独立状态量，避免多次绘制串扰)
        float currTick = 0;
        int8_t currbm = 0;
        float currCoordx = 0;
        float currCoordy = 0;
        float currCoordz = 0;
        float currOpa = 1.0;
        float currAngle = 0.0;
        float currSx = 0.0, currSy = 0.0;
        float currZx = 0.0, currZy = 0.0;
        float currOx = 0.0, currOy = 0.0;
        float currTimeOffset = 0.0;
        bool isNeedBp = false;
        float currbp[32] = {0.0};

        // CPU subdivision mesh data
        struct MeshVertex
        {
            float x, y; // clip space position
            float u, v; // texture coordinate
        };
        int _meshDivX = 8;
        int _meshDivY = 8;
        std::vector<MeshVertex> _meshVertices;
        std::vector<uint16_t> _meshIndices;
    };
    // motion辅助类 - 管理按priority排序的nodeList并处理子motion展开
    class emotemotionref
    {
    public:
        emotemotionref(emotemotion* mt, emoteengine* ee, emotenoderef* en = nullptr)
          : currentMotion(mt),
            refTop(ee),
            parent(en) {};
        ~emotemotionref();

        float getTickByIdx(int32_t parameterIdx);
        void progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
        void draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget);
        bool contains(tjs_real x, tjs_real y);
        const std::vector<emoterect>& getShapeList() const { return shapeList; }
        // 根据emotenode*查找对应的emotenoderef
        emotenoderef* getNodeRef(emotenode* node);

        emotemotion* currentMotion = nullptr;
        emoteengine* refTop = nullptr;
        std::vector<emoterect> shapeList;
        std::vector<emoteRender> renderMethod;
        emotenoderef* parent = nullptr;

        // 核心: 按priority排序的ref列表，平行于currentMotion->nodeList
        std::vector<emotenoderef> _nodeCache;
        // 子motion引用缓存(progress阶段创建，draw阶段使用)
        std::vector<emotemotionref*> _subMotionRefs;

        // shape节点区域(用于 getLayerGetter/getLayerMotion 的shape返回和contains检测)
        std::vector<emoterect> shapeNodeAreas;
    };
    // 核心模拟引擎
    class emoteengine
    {
    public:
	    // 主file/motion
        emotefile* _mainfile = nullptr;
        emotemotion* _mainmotion = nullptr;
	    // 附属file
        std::vector<emotefile*> _attach;
        float _zMax = 0.0f;

        ~emoteengine();

        // progress/draw接口(替代_mainMotionRef)
        void progress(float tick, std::vector<emoteRender>& renderList, emotelimit lim);
        void draw(krkrsdl3::iTVPRenderBackend* renderer, void* target, emotelimit lim, void* maskTarget);
        // 查找数值
        bool getTickByName(const std::string& name, tjs_real& retVal);

        void addEmoteFile(emotefile* itm);
        float getZMax();

        // 控制系列
        emotemotion* findmotionByName(const std::string& name);
        void updateEyeControl(float tick, bool isMain = false);
        std::vector<emotetimeline*> currTimeline;
        float currStartTick = -1.0f;
        void startTimeline(float tick, const std::string& name, bool isMain = false);
        void stopTimeline(const std::string& name, bool isMain = false);
        bool checkTimline(const std::string& name, bool& result, bool isMain = false);
        void updateTimelineControl(float tick, bool isMain = false);
        emoteVar* findVarByName(const std::string& name);
        void setVariable(const std::string& name, tjs_real value);
        tjs_real getVariable(const std::string& name);
        void updatePhysics(float tick);

        // 触摸命中判定：x/y 为 shapeNodeAreas 的收集空间
        //（渲染视口像素，左上原点、Y 向下）
        bool containsLabel(const std::string& label, float x, float y);
        // 无 label 过滤版：遍历全部 shape 判定层
        bool containsAnyShape(float x, float y);

        // progress阶段创建的主motion ref(生命周期跨progress/draw)
        emotemotionref* _mainMotionRef = nullptr;
        // 三重签名缓存变量数据
        std::map<std::string, tjs_real> _varCache;

        // shapeList(供emotemotionref::contains查询碰撞)
        std::vector<emoterect> shapeList;
    };
}