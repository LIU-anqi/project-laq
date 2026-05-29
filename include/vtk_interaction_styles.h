#pragma once
#ifndef VTK_INTERACTION_STYLES_H
#define VTK_INTERACTION_STYLES_H

#include <vtkInteractorStyleImage.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkResliceImageViewer.h>
#include <vtkCamera.h>
#include <vtkRenderer.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkCellPicker.h>
#include <vtkPropPicker.h>
#include <vtkObjectFactory.h>

// 引入主窗口头文件，以便调用 m_parent 的方法
#include "dicomviewer_3d.h"

// ============================================================
// 1. 2D 窗口交互样式 
// ============================================================
class MyViewStyle : public vtkInteractorStyleImage
{
public:
    static MyViewStyle* New();
    vtkTypeMacro(MyViewStyle, vtkInteractorStyleImage);

    void Initialize(dicomviewer_3d* parent, int index, vtkImageViewer2* viewer) {
        this->m_parent = parent;
        this->m_index = index;
        this->Viewer = viewer;
    }

    void UpdateResetValues(double w, double l, double minVal, double maxVal) {
        this->InitialWindow = w;
        this->InitialLevel = l;
        this->DataMin = minVal;
        this->DataMax = maxVal;
        if (Viewer) {
            this->InitialSlice = Viewer->GetSlice();
            if (Viewer->GetRenderer() && Viewer->GetRenderer()->GetActiveCamera()) {
                this->InitialParallelScale = Viewer->GetRenderer()->GetActiveCamera()->GetParallelScale();
            }
        }
    }

protected:
    dicomviewer_3d* m_parent = nullptr;
    int m_index = -1;
    vtkImageViewer2* Viewer = nullptr;

    double InitialWindow = 2000;
    double InitialLevel = 0;
    int InitialSlice = 0;
    double InitialParallelScale = 1.0;
    double DataMin = 0;
    double DataMax = 2000;
    bool IsWindowLeveling = false;
    int LastPos[2] = { 0, 0 };
    bool m_isDraggingCrosshair = false;

    // 标尺交互状态
    enum RulerDragMode { RD_NONE = 0, RD_TRANSLATE_V, RD_STRETCH_V, RD_TRANSLATE_H, RD_STRETCH_H };
    RulerDragMode m_rulerDragMode = RD_NONE;
    double m_rulerDragStartNorm = 0.0; // 拖动起点的 NormalizedViewport 坐标
    double m_rulerDragStartCenter = 0.0;
    double m_rulerDragStartHalf = 0.0;
    bool   m_rulerStretchIsEndB = false; // true=拖拽端点B(top/right), false=端点A(bottom/left)

    virtual void OnMouseWheelForward() override {
        if (!Viewer || !Viewer->GetInput()) return;
        if (this->Interactor->GetControlKey()) {
            vtkCamera* cam = Viewer->GetRenderer()->GetActiveCamera();
            double currentScale = cam->GetParallelScale();
            cam->SetParallelScale(currentScale * 0.9);
            if (m_parent) m_parent->updateRulerLayout(m_index);
            Viewer->Render();
        }
        else {
            vtkResliceImageViewer* resliceViewer = vtkResliceImageViewer::SafeDownCast(Viewer);
            bool isOblique = (resliceViewer && resliceViewer->GetResliceMode() == vtkResliceImageViewer::RESLICE_OBLIQUE);
            if (isOblique) {
                if (m_parent) m_parent->advanceSliceOblique(m_index, 1);
            }
            else {
                if (Viewer->GetSlice() < Viewer->GetSliceMax()) {
                    Viewer->SetSlice(Viewer->GetSlice() + 1);
                    if (m_parent) m_parent->updateOverlaySlice(m_index);
                    if (m_parent) m_parent->updateSliderUI(m_index, Viewer->GetSlice());
                    if (m_parent) m_parent->syncCrosshairToSlice(m_index);
                    Viewer->Render();
                }
            }
        }
    }

    virtual void OnMouseWheelBackward() override {
        if (!Viewer || !Viewer->GetInput()) return;
        if (this->Interactor->GetControlKey()) {
            vtkCamera* cam = Viewer->GetRenderer()->GetActiveCamera();
            double currentScale = cam->GetParallelScale();
            cam->SetParallelScale(currentScale * 1.1);
            if (m_parent) m_parent->updateRulerLayout(m_index);
            Viewer->Render();
        }
        else {
            vtkResliceImageViewer* resliceViewer = vtkResliceImageViewer::SafeDownCast(Viewer);
            bool isOblique = (resliceViewer && resliceViewer->GetResliceMode() == vtkResliceImageViewer::RESLICE_OBLIQUE);
            if (isOblique) {
                if (m_parent) m_parent->advanceSliceOblique(m_index, -1);
            }
            else {
                if (Viewer->GetSlice() > Viewer->GetSliceMin()) {
                    Viewer->SetSlice(Viewer->GetSlice() - 1);
                    if (m_parent) m_parent->updateOverlaySlice(m_index);
                    if (m_parent) m_parent->updateSliderUI(m_index, Viewer->GetSlice());
                    if (m_parent) m_parent->syncCrosshairToSlice(m_index);
                    Viewer->Render();
                }
            }
        }
    }

    virtual void OnMiddleButtonDown() override {}
    virtual void OnMiddleButtonUp() override {}

    virtual void OnLeftButtonDown() override {
        if (this->Interactor->GetRepeatCount() >= 1) {
            // 双击 ruler → 回归初始比例
            if (m_parent && Viewer && Viewer->GetInput()) {
                int* pos = this->Interactor->GetEventPosition();
                auto hitV = m_parent->hitTestRuler(m_index, true,  pos[0], pos[1]);
                auto hitH = m_parent->hitTestRuler(m_index, false, pos[0], pos[1]);
                if (hitV != dicomviewer_3d::RULER_NONE) {
                    auto& rv = m_parent->m_views[m_index].rulerV;
                    rv.halfLenNormInitial = 0.15; // 恢复默认锚定比例
                    rv.centerNorm = 0.5;
                    m_parent->updateRulerLayout(m_index);
                    Viewer->Render();
                    return;
                }
                if (hitH != dicomviewer_3d::RULER_NONE) {
                    auto& rh = m_parent->m_views[m_index].rulerH;
                    rh.halfLenNormInitial = 0.15; // 恢复默认锚定比例
                    rh.centerNorm = 0.5;
                    m_parent->updateRulerLayout(m_index);
                    Viewer->Render();
                    return;
                }
            }
            if (m_parent) m_parent->toggleMaximizeView(m_index);
            return;
        }
        if (this->Interactor->GetControlKey()) {
            if (!Viewer || !Viewer->GetInput()) return;
            int* clickPos = this->Interactor->GetEventPosition();
            vtkNew<vtkCellPicker> picker;
            picker->SetTolerance(0.005);
            picker->Pick(clickPos[0], clickPos[1], 0, Viewer->GetRenderer());
            double* worldPos = picker->GetPickPosition();
            if (m_parent) m_parent->handlePick(worldPos);
        }
        else if (this->Interactor->GetShiftKey() && m_parent && m_parent->isCrosshairVisible()) {
            // Shift+左键: 开始拖动十字线
            m_isDraggingCrosshair = true;
        }
        else {
            // 检测是否点击在标尺上
            if (m_parent && Viewer && Viewer->GetInput()) {
                int* pos = this->Interactor->GetEventPosition();
                int sx = pos[0], sy = pos[1];
                vtkRenderer* ren = Viewer->GetRenderer();
                int* vpSz = ren ? ren->GetSize() : nullptr;
                if (vpSz && vpSz[0] > 0 && vpSz[1] > 0) {
                    double nx = (double)sx / vpSz[0];
                    double ny = (double)sy / vpSz[1];
                    // 检测垂直标尺
                    auto hitV = m_parent->hitTestRuler(m_index, true, sx, sy);
                    if (hitV != dicomviewer_3d::RULER_NONE) {
                        if (hitV == dicomviewer_3d::RULER_BODY) {
                            m_rulerDragMode = RD_TRANSLATE_V;
                            m_rulerDragStartNorm = ny;
                            m_rulerDragStartCenter = m_parent->m_views[m_index].rulerV.centerNorm;
                        } else {
                            m_rulerDragMode = RD_STRETCH_V;
                            m_rulerStretchIsEndB = (hitV == dicomviewer_3d::RULER_END_B);
                            m_rulerDragStartNorm = ny;
                            m_rulerDragStartHalf = m_parent->m_views[m_index].rulerV.halfLenNorm;
                            m_rulerDragStartCenter = m_parent->m_views[m_index].rulerV.centerNorm;
                        }
                        return;
                    }
                    // 检测水平标尺
                    auto hitH = m_parent->hitTestRuler(m_index, false, sx, sy);
                    if (hitH != dicomviewer_3d::RULER_NONE) {
                        if (hitH == dicomviewer_3d::RULER_BODY) {
                            m_rulerDragMode = RD_TRANSLATE_H;
                            m_rulerDragStartNorm = nx;
                            m_rulerDragStartCenter = m_parent->m_views[m_index].rulerH.centerNorm;
                        } else {
                            m_rulerDragMode = RD_STRETCH_H;
                            m_rulerStretchIsEndB = (hitH == dicomviewer_3d::RULER_END_B);
                            m_rulerDragStartNorm = nx;
                            m_rulerDragStartHalf = m_parent->m_views[m_index].rulerH.halfLenNorm;
                            m_rulerDragStartCenter = m_parent->m_views[m_index].rulerH.centerNorm;
                        }
                        return;
                    }
                }
            }
            this->StartPan();
        }
    }
    virtual void OnLeftButtonUp() override {
        if (m_isDraggingCrosshair) {
            m_isDraggingCrosshair = false;
            return;
        }
        if (m_rulerDragMode != RD_NONE) {
            m_rulerDragMode = RD_NONE;
            return;
        }
        this->EndPan();
    }

    virtual void OnRightButtonDown() override {
        if (this->Interactor->GetControlKey()) {
            if (m_parent) m_parent->undoPick();
        }
        else {
            if (!Viewer || !Viewer->GetInput()) return;
            this->Interactor->GetEventPosition(this->LastPos);
            if (this->Interactor->GetRepeatCount() >= 1) {
                Viewer->SetColorWindow(InitialWindow);
                Viewer->SetColorLevel(InitialLevel);
                Viewer->SetSlice(InitialSlice);
                if (m_parent) {
                    m_parent->updateOverlaySlice(m_index);
                    m_parent->updateSliderUI(m_index, InitialSlice);
                }
                Viewer->GetRenderer()->ResetCamera();
                if (m_parent) m_parent->updateRulerLayout(m_index);
                Viewer->Render();
                this->IsWindowLeveling = false;
            }
            else {
                this->IsWindowLeveling = true;
            }
        }
    }
    virtual void OnRightButtonUp() override { this->IsWindowLeveling = false; }

    virtual void OnMouseMove() override {
        if (this->IsWindowLeveling && Viewer && Viewer->GetInput()) {
            int currPos[2];
            this->Interactor->GetEventPosition(currPos);
            int dx = currPos[0] - this->LastPos[0];
            int dy = currPos[1] - this->LastPos[1];
            this->LastPos[0] = currPos[0];
            this->LastPos[1] = currPos[1];
            double currentWindow = Viewer->GetColorWindow();
            double currentLevel = Viewer->GetColorLevel();
            double range = this->DataMax - this->DataMin;
            if (range <= 0) range = 1.0;
            double sensitivity = range / 800.0;
            double newWindow = currentWindow + dx * sensitivity;
            double newLevel = currentLevel - dy * sensitivity;
            if (newWindow < 0.01) newWindow = 0.01;
            Viewer->SetColorWindow(newWindow);
            Viewer->SetColorLevel(newLevel);
            Viewer->Render();
        }
        else {
            // 标尺拖动交互
            if (m_rulerDragMode != RD_NONE && m_parent && Viewer && Viewer->GetInput()) {
                int* pos = this->Interactor->GetEventPosition();
                vtkRenderer* ren = Viewer->GetRenderer();
                int* vpSz = ren ? ren->GetSize() : nullptr;
                if (vpSz && vpSz[0] > 0 && vpSz[1] > 0) {
                    double nx = (double)pos[0] / vpSz[0];
                    double ny = (double)pos[1] / vpSz[1];
                    vtkCamera* cam = ren->GetActiveCamera();
                    double ps = cam ? cam->GetParallelScale() : 100.0;
                    double viewHMM = 2.0 * ps;
                    double viewWMM = viewHMM * ((double)vpSz[0] / vpSz[1]);

                    if (m_rulerDragMode == RD_TRANSLATE_V) {
                        // 平移：只移动 centerNorm，直接刷新端点（不重算量程，不调 updateRulerLayout）
                        double delta = ny - m_rulerDragStartNorm;
                        auto& rv = m_parent->m_views[m_index].rulerV;
                        rv.centerNorm = m_rulerDragStartCenter + delta;
                        double padV = rv.halfLenNorm + 0.01;
                        if (rv.centerNorm < padV)        rv.centerNorm = padV;
                        if (rv.centerNorm > 1.0 - padV)  rv.centerNorm = 1.0 - padV;
                        if (rv.axis) {
                            double x = 0.92;
                            rv.axis->GetPoint1Coordinate()->SetValue(x, rv.centerNorm - rv.halfLenNorm);
                            rv.axis->GetPoint2Coordinate()->SetValue(x, rv.centerNorm + rv.halfLenNorm);
                            rv.axis->Modified();
                        }
                        Viewer->Render();
                    } else if (m_rulerDragMode == RD_TRANSLATE_H) {
                        double delta = nx - m_rulerDragStartNorm;
                        auto& rh = m_parent->m_views[m_index].rulerH;
                        rh.centerNorm = m_rulerDragStartCenter + delta;
                        double padH = rh.halfLenNorm + 0.01;
                        if (rh.centerNorm < padH)        rh.centerNorm = padH;
                        if (rh.centerNorm > 1.0 - padH)  rh.centerNorm = 1.0 - padH;
                        if (rh.axis) {
                            double y = 0.06;
                            rh.axis->GetPoint1Coordinate()->SetValue(rh.centerNorm - rh.halfLenNorm, y);
                            rh.axis->GetPoint2Coordinate()->SetValue(rh.centerNorm + rh.halfLenNorm, y);
                            rh.axis->Modified();
                        }
                        Viewer->Render();
                    } else if (m_rulerDragMode == RD_STRETCH_V) {
                        // 对称拉伸：更新锚定比例, updateRulerLayout 负责 snap 到整十
                        double delta = ny - m_rulerDragStartNorm;
                        if (!m_rulerStretchIsEndB) delta = -delta;
                        double newHalf = m_rulerDragStartHalf + delta;
                        if (newHalf < 0.02) newHalf = 0.02;
                        if (newHalf > 0.48) newHalf = 0.48;
                        m_parent->m_views[m_index].rulerV.halfLenNormInitial = newHalf;
                        m_parent->updateRulerLayout(m_index);
                        Viewer->Render();
                    } else if (m_rulerDragMode == RD_STRETCH_H) {
                        double delta = nx - m_rulerDragStartNorm;
                        if (!m_rulerStretchIsEndB) delta = -delta;
                        double newHalf = m_rulerDragStartHalf + delta;
                        if (newHalf < 0.02) newHalf = 0.02;
                        if (newHalf > 0.48) newHalf = 0.48;
                        m_parent->m_views[m_index].rulerH.halfLenNormInitial = newHalf;
                        m_parent->updateRulerLayout(m_index);
                        Viewer->Render();
                    }
                }
                return;
            }
            // 十字线拖动
            if (m_isDraggingCrosshair && m_parent && Viewer && Viewer->GetInput()) {
                int* pos = this->Interactor->GetEventPosition();
                vtkRenderer* ren = Viewer->GetRenderer();
                if (ren) {
                    ren->SetDisplayPoint(pos[0], pos[1], 0.5);
                    ren->DisplayToWorld();
                    double* wp = ren->GetWorldPoint();
                    if (wp[3] != 0.0) {
                        double worldPos[3] = { wp[0]/wp[3], wp[1]/wp[3], wp[2]/wp[3] };
                        m_parent->updateCrosshairPosition(m_index, worldPos);
                    }
                }
                return;
            }
            // Hover preview for AC-PC calibration mode
            if (m_parent && m_parent->getAcpcPickMode() != dicomviewer_3d::ACPC_NONE
                && Viewer && Viewer->GetInput()) {
                int* pos = this->Interactor->GetEventPosition();
                m_parent->handleHover(m_index, pos[0], pos[1]);
            }
            vtkInteractorStyleImage::OnMouseMove();
        }
    }
};
vtkStandardNewMacro(MyViewStyle);


// ============================================================
// 2. 3D 窗口交互样式
// ============================================================
class Slicer3DStyle : public vtkInteractorStyleTrackballCamera
{
public:
    static Slicer3DStyle* New();
    vtkTypeMacro(Slicer3DStyle, vtkInteractorStyleTrackballCamera);

    void Initialize(dicomviewer_3d* parent) {
        this->m_parent = parent;
    }

    virtual void OnLeftButtonDown() override {
        if (this->Interactor->GetRepeatCount() >= 1) {
            if (m_parent) m_parent->toggleMaximizeView(-1);
            return;
        }
        if (this->Interactor->GetControlKey()) {
            this->StartSpin();
        }
        else if (this->Interactor->GetShiftKey()) {
            int* clickPos = this->Interactor->GetEventPosition();
            vtkRenderer* ren = this->Interactor->FindPokedRenderer(clickPos[0], clickPos[1]);
            if (ren) {
                vtkNew<vtkPropPicker> picker;
                picker->Pick(clickPos[0], clickPos[1], 0.0, ren);
                if (m_parent && m_parent->isMainLeadPicked(picker)) {
                    m_isDraggingDepth = true;
                    m_lastY = clickPos[1];
                    return;
                }
            }
            this->StartPan();
        }
        else {
            int* clickPos = this->Interactor->GetEventPosition();
            vtkRenderer* ren = this->Interactor->FindPokedRenderer(clickPos[0], clickPos[1]);
            if (ren) {
                vtkNew<vtkPropPicker> picker;
                picker->Pick(clickPos[0], clickPos[1], 0.0, ren);
                if (m_parent && m_parent->isMainLeadPicked(picker)) {
                    m_isDraggingLeadXY = true;
                    m_lastX = clickPos[0];
                    m_lastY = clickPos[1];
                    picker->GetPickPosition(m_pickWorldPos);
                    return;
                }
            }
            this->StartRotate();
        }
    }

    virtual void OnRightButtonDown() override {
        if (this->Interactor->GetControlKey()) {
            Pick3D();
        }
        else if (this->Interactor->GetShiftKey()) {
            if (m_parent) m_parent->undoPick();
        }
        else {
            this->StartDolly();
        }
    }

    virtual void OnMiddleButtonDown() override { this->StartPan(); }
    virtual void OnLeftButtonUp() override { 
        if (m_isDraggingDepth) {
            m_isDraggingDepth = false;
            return;
        }
        if (m_isDraggingLeadXY) {
            m_isDraggingLeadXY = false;
            return;
        }
        this->EndSpin(); this->EndRotate(); this->EndPan(); 
    }
    virtual void OnMiddleButtonUp() override { this->EndPan(); }
    virtual void OnRightButtonUp() override { this->EndDolly(); }

    virtual void OnMouseMove() override {
        if (m_isDraggingDepth) {
            int currentY = this->Interactor->GetEventPosition()[1];
            int dy = currentY - m_lastY;
            m_lastY = currentY;
            if (dy != 0 && m_parent) {
                // 向外发射增量偏移
                m_parent->dragMainLeadDepth(dy * 0.05);
            }
            return;
        }
        
        if (m_isDraggingLeadXY) {
            int* currentPos = this->Interactor->GetEventPosition();
            vtkRenderer* ren = this->DefaultRenderer;
            if (!ren) ren = this->Interactor->FindPokedRenderer(currentPos[0], currentPos[1]);
            if (ren && m_parent) {
                ren->SetWorldPoint(m_pickWorldPos[0], m_pickWorldPos[1], m_pickWorldPos[2], 1.0);
                ren->WorldToDisplay();
                double displayZ = ren->GetDisplayPoint()[2];

                ren->SetDisplayPoint(m_lastX, m_lastY, displayZ);
                ren->DisplayToWorld();
                double world1[4];
                ren->GetWorldPoint(world1);

                ren->SetDisplayPoint(currentPos[0], currentPos[1], displayZ);
                ren->DisplayToWorld();
                double world2[4];
                ren->GetWorldPoint(world2);

                if (world1[3] != 0.0 && world2[3] != 0.0) {
                    double offset[3] = {
                        (world2[0]/world2[3]) - (world1[0]/world1[3]),
                        (world2[1]/world2[3]) - (world1[1]/world1[3]),
                        (world2[2]/world2[3]) - (world1[2]/world1[3])
                    };
                    m_parent->translateMainLead(offset);
                    
                    m_pickWorldPos[0] += offset[0];
                    m_pickWorldPos[1] += offset[1];
                    m_pickWorldPos[2] += offset[2];
                }
            }
            m_lastX = currentPos[0];
            m_lastY = currentPos[1];
            return;
        }
        vtkInteractorStyleTrackballCamera::OnMouseMove();
    }

protected:
    dicomviewer_3d* m_parent = nullptr;
    bool m_isDraggingDepth = false;
    bool m_isDraggingLeadXY = false;
    int m_lastY = 0;
    int m_lastX = 0;
    double m_pickWorldPos[3] = {0.0, 0.0, 0.0};

    void Pick3D() {
        if (!m_parent) return;
        int* clickPos = this->Interactor->GetEventPosition();
        vtkRenderer* renderer = this->Interactor->FindPokedRenderer(clickPos[0], clickPos[1]);
        if (!renderer) return;

        vtkNew<vtkCellPicker> picker;
        picker->SetTolerance(0.005);
        int pickResult = picker->Pick(clickPos[0], clickPos[1], 0, renderer);
        if (pickResult != 0) {
            double* worldPos = picker->GetPickPosition();
            m_parent->handlePick(worldPos);
        }
    }
};
vtkStandardNewMacro(Slicer3DStyle);

#endif // VTK_INTERACTION_STYLES_H