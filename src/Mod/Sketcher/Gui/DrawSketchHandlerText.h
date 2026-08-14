// SPDX - License - Identifier: LGPL - 2.1 - or -later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2025 Pierre-Louis Boyer                                  *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/


#ifndef SKETCHERGUI_DrawSketchHandlerText_H
#define SKETCHERGUI_DrawSketchHandlerText_H

#include <QMap>

#include <Gui/BitmapFactory.h>
#include <Gui/Notifications.h>
#include <Gui/Command.h>
#include <Gui/CommandT.h>
#include <Gui/InputHint.h>

#include <Mod/Sketcher/App/SketchObject.h>

#include "DrawSketchDefaultWidgetController.h"
#include "DrawSketchControllableHandler.h"

#include "Utils.h"
#include "CommandConstraints.h"

#include <vector>
#include <algorithm>

namespace SketcherGui
{

class DrawSketchHandlerText;

// The Create Text tool has no construction-method selector: every text gets both a width and a
// height construction line, and its scaling behaviour is emergent from the constraint state
// (see SketchObject::textShouldStretch and the TextAspectRatio lock). The single combobox is Font.
using DSHTextController = DrawSketchDefaultWidgetController<
    DrawSketchHandlerText,
    /*SelectModeT*/ StateMachines::TwoSeekEnd,
    /*PAutoConstraintSize =*/2,
    /*OnViewParametersT =*/OnViewParameters<4, 4>,  // NOLINT
    /*WidgetParametersT =*/WidgetParameters<0, 0>,  // NOLINT
    /*WidgetCheckboxesT =*/WidgetCheckboxes<0, 0>,  // NOLINT
    /*WidgetComboboxesT =*/WidgetComboboxes<1, 1>,  // NOLINT  (Font only)
    /*WidgetLineEditsT =*/WidgetLineEdits<1, 1>>;    // NOLINT  (Text string)

using DSHTextControllerBase = DSHTextController::ControllerBase;

using DrawSketchHandlerTextBase = DrawSketchControllableHandler<DSHTextController>;


class DrawSketchHandlerText: public DrawSketchHandlerTextBase
{
    friend DSHTextController;
    friend DSHTextControllerBase;

public:
    explicit DrawSketchHandlerText(ConstructionMethod constrMethod = ConstructionMethod::End)
        : DrawSketchHandlerTextBase(constrMethod)
        , length(0.0)
        , handleId(0)
        , text(QObject::tr("Text").toStdString())
        , font("")
        , cachedTextName("")
        , cachedFontName("")
        , cachedBaseShapes({}) {};
    ~DrawSketchHandlerText() override = default;

private:
    void updateDataAndDrawToPosition(Base::Vector2d onSketchPos) override
    {
        switch (state()) {
            case SelectMode::SeekFirst: {
                toolWidgetManager.drawPositionAtCursor(onSketchPos);

                startPoint = onSketchPos;

                seekAndRenderAutoConstraint(sugConstraints[0], onSketchPos, Base::Vector2d(0.f, 0.f));
            } break;
            case SelectMode::SeekSecond: {
                toolWidgetManager.drawDirectionAtCursor(onSketchPos, startPoint);

                endPoint = onSketchPos;

                try {
                    CreateAndDrawShapeGeometry();
                }
                catch (const Base::ValueError&) {
                }  // equal points while hovering raise an objection that can be safely ignored

                seekAndRenderAutoConstraint(sugConstraints[1], onSketchPos, onSketchPos - startPoint);
            } break;
            default:
                break;
        }
    }

    void executeCommands() override
    {
        try {
            openCommand(QT_TRANSLATE_NOOP("Command", "Add sketch Text"));

            // The interactively drawn line is always the width/baseline line.
            Base::Vector2d widthVec = endPoint - startPoint;
            double widthLength = widthVec.Length();
            if (widthLength < Precision::Confusion()) {
                abortCommand();
                return;
            }

            // Measure the text's natural aspect (height/width) so the auto-generated
            // perpendicular height line starts undistorted, and so the default
            // TextAspectRatio lock is created with the correct ratio k = baseHeight/baseWidth.
            double aspect = 1.0;
            {
                std::vector<TopoDS_Shape> shapes;
                if (cachedTextName == text && cachedFontName == font && !cachedBaseShapes.empty()) {
                    shapes = cachedBaseShapes;
                }
                else if (!font.empty()) {
                    shapes = Part::makeTextWires(text, font);
                }
                double bw = 0.0;
                double bh = 0.0;
                if (Part::measureTextShapesBoundingBox(shapes, bw, bh)
                    && bw > Precision::Confusion()) {
                    aspect = bh / bw;
                }
            }
            if (aspect < Precision::Confusion()) {
                aspect = 1.0;
            }

            // Height line: perpendicular to the width line, sharing the start point,
            // length = aspect * widthLength (so the text is undistorted at creation).
            Base::Vector2d perp(-widthVec.y, widthVec.x);
            if (perp.Length() > Precision::Confusion()) {
                perp = perp / perp.Length();
            }
            else {
                perp = Base::Vector2d(0.0, 1.0);
            }
            Base::Vector2d heightEnd = startPoint + perp * (aspect * widthLength);

            // 1. Add the width (baseline) construction line.
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addGeometry(Part.LineSegment(App.Vector(%f, %f,0), App.Vector(%f, %f,0)), True)",
                startPoint.x,
                startPoint.y,
                endPoint.x,
                endPoint.y
            );
            int widthId = getHighestCurveIndex();
            handleId = widthId;

            // 2. Add the perpendicular height construction line, sharing the start point.
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addGeometry(Part.LineSegment(App.Vector(%f, %f,0), App.Vector(%f, %f,0)), True)",
                startPoint.x,
                startPoint.y,
                heightEnd.x,
                heightEnd.y
            );
            int heightId = getHighestCurveIndex();

            // 3. Anchor the two lines together and keep them perpendicular.
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addConstraint(Sketcher.Constraint('Coincident', %d, 1, %d, 1))",
                widthId,
                heightId
            );
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addConstraint(Sketcher.Constraint('Perpendicular', %d, %d))",
                widthId,
                heightId
            );

            // 4. Default aspect-ratio lock (|height| = k*|width|), so a single dimension
            //    on either line fully determines the text size (uniform, undistorted).
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addConstraint(Sketcher.Constraint('TextAspectRatio', %d, %d, %f))",
                widthId,
                heightId,
                aspect
            );

            std::string escText = escapeForPython(text);
            std::string escFontPath = escapeForPython(font);
            const char* constrBoolStr = isConstructionMode() ? "True" : "False";

            // 5. Add the dual-line 'Text' constraint. element[0]=width, element[1]=height.
            // Constructed with two leading line references it is stamped schema=2.
            // We do not add the glyph geometry manually to avoid floating-point precision loss
            // associated with Python serialization.
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "addConstraint(Sketcher.Constraint('Text', [%d, 0, %d, 0], '%s', '%s', False))",
                widthId,
                heightId,
                escText.c_str(),
                escFontPath.c_str()
            );

            // 6. Generate the glyph geometry via setTextAndFont on the new Text constraint
            // (last in the list). This runs the C++ path that inserts exact closed wires.
            Gui::cmdAppObjectArgs(
                getSketchObject(),
                "setTextAndFont(len(App.ActiveDocument.getObject('%s').Constraints)-1, '%s', '%s', "
                "False, %s)",
                getSketchObject()->getNameInDocument(),
                escText.c_str(),
                escFontPath.c_str(),
                constrBoolStr
            );

            commitCommand();
        }
        catch (const Base::Exception& e) {
            Gui::NotifyError(
                sketchgui,
                QT_TRANSLATE_NOOP("Notifications", "Error"),
                QT_TRANSLATE_NOOP("Notifications", "Failed to add text")
            );

            abortCommand();
        }
    }

    void generateAutoConstraints() override
    {
        // Generate temporary autoconstraints (but do not actually add them to the sketch)
        if (avoidRedundants) {
            removeRedundantHorizontalVertical(getSketchObject(), sugConstraints[0], sugConstraints[1]);
        }

        auto& ac1 = sugConstraints[0];
        auto& ac2 = sugConstraints[1];

        generateAutoConstraintsOnElement(ac1, handleId, Sketcher::PointPos::start);
        generateAutoConstraintsOnElement(ac2, handleId, Sketcher::PointPos::end);

        // Ensure temporary autoconstraints do not generate a redundancy and that the geometry
        // parameters are accurate This is particularly important for adding widget mandated
        // constraints.
        removeRedundantAutoConstraints();
    }

    void createAutoConstraints() override
    {
        // execute python command to create autoconstraints
        createGeneratedAutoConstraints(true);

        sugConstraints[0].clear();
        sugConstraints[1].clear();
    }

    std::string getToolName() const override
    {
        return "DSH_Text";
    }

    QString getCrosshairCursorSVGName() const override
    {
        return QStringLiteral("Sketcher_Pointer_Text.svg");
    }

    std::unique_ptr<QWidget> createWidget() const override
    {
        return std::make_unique<SketcherToolDefaultWidget>();
    }

    bool isWidgetVisible() const override
    {
        return true;  // Text tool must show the line edit to make sense
    };

    QPixmap getToolIcon() const override
    {
        return Gui::BitmapFactory().pixmap("Sketcher_CreateText");
    }

    QString getToolWidgetText() const override
    {
        return QString(QObject::tr("Text parameters"));
    }

    bool canGoToNextMode() override
    {
        if (state() == SelectMode::SeekSecond && length < Precision::Confusion()) {
            // Prevent validation of null Text.
            return false;
        }
        return true;
    }

    void angleSnappingControl() override
    {
        if (state() == SelectMode::SeekSecond) {
            setAngleSnapping(true, startPoint);
        }

        else {
            setAngleSnapping(false);
        }
    }

private:
    QMap<QString, QString> fontPathMap;
    Base::Vector2d startPoint, endPoint;
    double length;
    int handleId;

    std::string text;
    std::string font;
    std::string cachedTextName;
    std::string cachedFontName;
    std::vector<TopoDS_Shape> cachedBaseShapes;

    void createShape(bool onlyeditoutline) override
    {
        ShapeGeometry.clear();

        Base::Vector2d vecL = endPoint - startPoint;
        length = vecL.Length();
        if (length < Precision::Confusion()) {
            return;
        }

        // 1. Check if the cache is valid. If the user selected a new file,
        // or if the cache is empty, we need to re-load from the SVG.
        if (cachedTextName != text || cachedFontName != font || cachedBaseShapes.empty()) {
            if (!font.empty()) {
                cachedTextName = text;
                cachedFontName = font;
                // This is the one-time slow operation to get the template shapes.
                cachedBaseShapes = Part::makeTextWires(text, font);
            }
            else {
                cachedBaseShapes.clear();
            }
        }

        // 2. Call the generic helper to transform and create the final geometry.
        // The interactively drawn line is always the width/baseline, so the preview
        // is rendered uniformly (isHeight = false).
        transformAndConvertToGeometry(
            ShapeGeometry,
            cachedBaseShapes,
            toVector3d(startPoint),
            toVector3d(endPoint),
            false
        );

        // 3. Set construction mode on the newly created geometry
        if (isConstructionMode() && !onlyeditoutline) {
            for (auto& geo : ShapeGeometry) {
                Sketcher::GeometryFacade::setConstruction(geo.get(), true);
            }
        }
    }

    std::list<Gui::InputHint> getToolHints() const override
    {
        return lookupTextHints(static_cast<int>(state()));
    }

    struct HintEntry
    {
        int state;
        std::list<Gui::InputHint> hints;
    };

    using HintTable = std::vector<HintEntry>;

    static HintTable getTextHintTable();
    static std::list<Gui::InputHint> lookupTextHints(int state);
};

template<>
auto DSHTextControllerBase::getState(int labelindex) const
{
    switch (labelindex) {
        case OnViewParameter::First:
        case OnViewParameter::Second:
            return SelectMode::SeekFirst;
            break;
        case OnViewParameter::Third:
        case OnViewParameter::Fourth:
            return SelectMode::SeekSecond;
            break;
        default:
            THROWM(Base::ValueError, "Label index without an associated machine state")
    }
}

template<>
void DSHTextController::configureToolWidget()
{
    if (!init) {  // Code to be executed only upon initialisation
        toolWidget->setLineEditLabel(
            WLineEdit::FirstEdit,
            QApplication::translate("TaskSketcherTool_Text", "Text")
        );
        toolWidget->setLineEditText(WLineEdit::FirstEdit, QString::fromStdString(handler->text));

        toolWidget->setComboboxLabel(
            WCombobox::FirstCombo,
            QApplication::translate("TaskSketcherTool_Text", "Font")
        );

        // 1. Scan for font files and store the map
        handler->fontPathMap = findAvailableFontFiles();

        // 2. Populate combobox with friendly names (the keys of the map)
        QStringList fontNames = handler->fontPathMap.keys();
        fontNames.sort(Qt::CaseInsensitive);
        toolWidget->setComboboxElements(WCombobox::FirstCombo, fontNames);

        // 3. Set a sensible default font
        QString defaultFontName;
        if (fontNames.contains(QString::fromUtf8("osifont-lgpl3fe"), Qt::CaseInsensitive)) {
            defaultFontName = QString::fromUtf8("osifont-lgpl3fe");
        }
        else if (fontNames.contains(QString::fromUtf8("DejaVu Sans"), Qt::CaseInsensitive)) {
            defaultFontName = QString::fromUtf8("DejaVu Sans");
        }
        else if (fontNames.contains(QString::fromUtf8("Arial"), Qt::CaseInsensitive)) {
            defaultFontName = QString::fromUtf8("Arial");
        }
        else if (!fontNames.isEmpty()) {
            defaultFontName = fontNames.first();
        }

        if (!defaultFontName.isEmpty()) {
            // Find the actual case-sensitive key
            for (const auto& key : fontNames) {
                if (key.compare(defaultFontName, Qt::CaseInsensitive) == 0) {
                    handler->font = handler->fontPathMap.value(key).toStdString();
                    toolWidget->setComboboxCurrentText(WCombobox::FirstCombo, key);
                    break;
                }
            }
        }
    }

    onViewParameters[OnViewParameter::First]->setLabelType(Gui::SoDatumLabel::DISTANCEX);
    onViewParameters[OnViewParameter::Second]->setLabelType(Gui::SoDatumLabel::DISTANCEY);

    onViewParameters[OnViewParameter::Third]->setLabelType(
        Gui::SoDatumLabel::DISTANCE,
        Gui::EditableDatumLabel::Function::Dimensioning
    );
    onViewParameters[OnViewParameter::Fourth]->setLabelType(
        Gui::SoDatumLabel::ANGLE,
        Gui::EditableDatumLabel::Function::Dimensioning
    );

    toolWidget->setLineEditText(
        SketcherToolDefaultWidget::LineEdit::FirstEdit,
        QString::fromStdString(handler->text)
    );
}

template<>
void DSHTextController::adaptDrawingToLineEditTextChange(int lineeditindex, const QString& value)
{
    if (lineeditindex == WLineEdit::FirstEdit) {
        handler->text = value.toStdString();
        // The redraw is handled by the controller's finishControlsChanged()
    }
}

template<>
void DSHTextController::adaptDrawingToComboboxChange(int comboboxindex, int value)
{
    Q_UNUSED(value);
    if (comboboxindex == WCombobox::FirstCombo) {
        // Get the selected friendly name
        QString fontName = toolWidget->getComboboxCurrentText(WCombobox::FirstCombo);
        // Look up the full path in our map and update the handler
        if (handler->fontPathMap.contains(fontName)) {
            handler->font = handler->fontPathMap.value(fontName).toStdString();
        }
        // The redraw is handled by the controller's finishControlsChanged()
    }
}

template<>
void DSHTextControllerBase::doEnforceControlParameters(Base::Vector2d& onSketchPos)
{
    switch (handler->state()) {
        case SelectMode::SeekFirst: {
            auto& firstParam = onViewParameters[OnViewParameter::First];
            auto& secondParam = onViewParameters[OnViewParameter::Second];

            if (firstParam->isSet) {
                onSketchPos.x = firstParam->getValue();
            }

            if (secondParam->isSet) {
                onSketchPos.y = secondParam->getValue();
            }
        } break;
        case SelectMode::SeekSecond: {
            auto& thirdParam = onViewParameters[OnViewParameter::Third];
            auto& fourthParam = onViewParameters[OnViewParameter::Fourth];

            Base::Vector2d dir = onSketchPos - handler->startPoint;
            if (dir.Length() < Precision::Confusion()) {
                dir.x = 1.0;  // if direction null, default to (1,0)
            }
            double length = dir.Length();

            if (thirdParam->isSet) {
                length = thirdParam->getValue();
                if (length < Precision::Confusion()) {
                    unsetOnViewParameter(thirdParam.get());
                    return;
                }

                onSketchPos = handler->startPoint + length * dir.Normalize();
            }

            if (fourthParam->isSet) {
                double angle = Base::toRadians(fourthParam->getValue());
                Base::Vector2d dir(cos(angle), sin(angle));
                onSketchPos.ProjectToLine(onSketchPos - handler->startPoint, dir);
                onSketchPos += handler->startPoint;
            }

            if (thirdParam->isSet && fourthParam->isSet
                && (onSketchPos - handler->startPoint).Length() < Precision::Confusion()) {
                unsetOnViewParameter(thirdParam.get());
                unsetOnViewParameter(fourthParam.get());
            }
        } break;
        default:
            break;
    }
}

template<>
void DSHTextController::adaptParameters(Base::Vector2d onSketchPos)
{
    switch (handler->state()) {
        case SelectMode::SeekFirst: {
            auto& firstParam = onViewParameters[OnViewParameter::First];
            auto& secondParam = onViewParameters[OnViewParameter::Second];

            if (!firstParam->isSet) {
                setOnViewParameterValue(OnViewParameter::First, onSketchPos.x);
            }

            if (!secondParam->isSet) {
                setOnViewParameterValue(OnViewParameter::Second, onSketchPos.y);
            }

            bool sameSign = onSketchPos.x * onSketchPos.y > 0.;
            firstParam->setLabelAutoDistanceReverse(!sameSign);
            secondParam->setLabelAutoDistanceReverse(sameSign);
            firstParam->setPoints(Base::Vector3d(), toVector3d(onSketchPos));
            secondParam->setPoints(Base::Vector3d(), toVector3d(onSketchPos));
        } break;
        case SelectMode::SeekSecond: {
            auto& thirdParam = onViewParameters[OnViewParameter::Third];
            auto& fourthParam = onViewParameters[OnViewParameter::Fourth];

            Base::Vector3d start = toVector3d(handler->startPoint);
            Base::Vector3d end = toVector3d(handler->endPoint);
            Base::Vector3d vec = end - start;

            if (!thirdParam->isSet) {
                setOnViewParameterValue(OnViewParameter::Third, vec.Length());
            }

            double range = (handler->endPoint - handler->startPoint).Angle();


            if (!fourthParam->isSet) {
                setOnViewParameterValue(
                    OnViewParameter::Fourth,
                    Base::toDegrees(range),
                    Base::Unit::Angle
                );
            }
            else if (fourthParam->hasFinishedEditing && vec.Length() > Precision::Confusion()) {
                double ovpRange = Base::toRadians(fourthParam->getValue());
                if (fabs(range - ovpRange) > Precision::Confusion()) {
                    setOnViewParameterValue(
                        OnViewParameter::Fourth,
                        Base::toDegrees(range),
                        Base::Unit::Angle
                    );
                }
            }

            thirdParam->setPoints(start, end);
            fourthParam->setPoints(start, Base::Vector3d());
            fourthParam->setLabelRange(range);
        } break;
        default:
            break;
    }
}

template<>
void DSHTextController::computeNextDrawSketchHandlerMode()
{
    switch (handler->state()) {
        case SelectMode::SeekFirst: {
            auto& firstParam = onViewParameters[OnViewParameter::First];
            auto& secondParam = onViewParameters[OnViewParameter::Second];

            if (firstParam->hasFinishedEditing && secondParam->hasFinishedEditing) {
                handler->setNextState(SelectMode::SeekSecond);
            }
        } break;
        case SelectMode::SeekSecond: {
            auto& thirdParam = onViewParameters[OnViewParameter::Third];
            auto& fourthParam = onViewParameters[OnViewParameter::Fourth];

            if (thirdParam->hasFinishedEditing && fourthParam->hasFinishedEditing) {
                handler->setNextState(SelectMode::End);
            }
        } break;
        default:
            break;
    }
}

template<>
void DSHTextController::addConstraints()
{
    App::DocumentObject* obj = handler->sketchgui->getObject();

    int firstCurve = handler->handleId;

    auto x0 = onViewParameters[OnViewParameter::First]->getValue();
    auto y0 = onViewParameters[OnViewParameter::Second]->getValue();
    auto p3 = onViewParameters[OnViewParameter::Third]->getValue();
    auto p4 = onViewParameters[OnViewParameter::Fourth]->getValue();

    auto x0set = onViewParameters[OnViewParameter::First]->isSet;
    auto y0set = onViewParameters[OnViewParameter::Second]->isSet;
    auto p3set = onViewParameters[OnViewParameter::Third]->isSet;
    auto p4set = onViewParameters[OnViewParameter::Fourth]->isSet;

    using namespace Sketcher;

    auto constraintToOrigin = [&]() {
        ConstraintToAttachment(GeoElementId(firstCurve, PointPos::start), GeoElementId::RtPnt, x0, obj);
    };

    auto constraintx0 = [&]() {
        ConstraintToAttachment(GeoElementId(firstCurve, PointPos::start), GeoElementId::VAxis, x0, obj);
    };

    auto constrainty0 = [&]() {
        ConstraintToAttachment(GeoElementId(firstCurve, PointPos::start), GeoElementId::HAxis, y0, obj);
    };

    auto constraintp3length = [&]() {
        Gui::cmdAppObjectArgs(
            obj,
            "addConstraint(Sketcher.Constraint('Distance',%d,%f)) ",
            firstCurve,
            fabs(p3)
        );
    };

    auto constraintp4angle = [&]() {
        double angle = Base::toRadians(p4);
        ConstraintLineByAngle(firstCurve, angle, obj);
    };

    if (handler->AutoConstraints.empty()) {  // No valid diagnosis. Every constraint can be added.

        if (x0set && y0set && x0 == 0. && y0 == 0.) {
            constraintToOrigin();
        }
        else {
            if (x0set) {
                constraintx0();
            }

            if (y0set) {
                constrainty0();
            }
        }

        if (p3set) {
            constraintp3length();
        }

        if (p4set) {
            constraintp4angle();
        }
    }
    else {  // Valid diagnosis. Must check which constraints may be added.
        auto startpointinfo = handler->getPointInfo(GeoElementId(firstCurve, PointPos::start));

        if (x0set && startpointinfo.isXDoF()) {
            constraintx0();

            handler->diagnoseWithAutoConstraints();  // ensure we have recalculated parameters after
                                                     // each constraint addition

            startpointinfo = handler->getPointInfo(
                GeoElementId(firstCurve, PointPos::start)
            );  // get updated point position
        }

        if (y0set && startpointinfo.isYDoF()) {
            constrainty0();

            handler->diagnoseWithAutoConstraints();  // ensure we have recalculated parameters after
                                                     // each constraint addition

            startpointinfo = handler->getPointInfo(
                GeoElementId(firstCurve, PointPos::start)
            );  // get updated point position
        }

        auto endpointinfo = handler->getPointInfo(GeoElementId(firstCurve, PointPos::end));

        int DoFs = startpointinfo.getDoFs();
        DoFs += endpointinfo.getDoFs();

        if (p3set && DoFs > 0) {
            constraintp3length();
            DoFs--;
        }

        if (p4set && DoFs > 0) {
            constraintp4angle();
        }
    }
}

DrawSketchHandlerText::HintTable DrawSketchHandlerText::getTextHintTable()
{
    return {
        // Structure: {state, {hints...}}
        {0, {{QObject::tr("%1 pick start point"), {Gui::InputHint::UserInput::MouseLeft}}}},
        {1, {{QObject::tr("%1 pick end point"), {Gui::InputHint::UserInput::MouseLeft}}}}
    };
}

std::list<Gui::InputHint> DrawSketchHandlerText::lookupTextHints(int state)
{
    const auto TextHintTable = getTextHintTable();

    auto it = std::find_if(
        TextHintTable.begin(),
        TextHintTable.end(),
        [state](const HintEntry& entry) {
            return entry.state == state;
        }
    );

    return (it != TextHintTable.end()) ? it->hints : std::list<Gui::InputHint> {};
}

}  // namespace SketcherGui


#endif  // SKETCHERGUI_DrawSketchHandlerText_H
