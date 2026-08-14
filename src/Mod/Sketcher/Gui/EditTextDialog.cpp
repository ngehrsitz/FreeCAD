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

#include "PreCompiled.h"
#ifndef _PreComp_
# include <QComboBox>
# include <QLineEdit>
# include <QMessageBox>
#endif

#include <Gui/CommandT.h>
#include <Mod/Sketcher/App/SketchObject.h>

#include "CommandConstraints.h"
#include "EditTextDialog.h"
#include "ViewProviderSketch.h"
#include "Utils.h"
#include "ui_EditTextDialog.h"

using namespace SketcherGui;

EditTextDialog::EditTextDialog(ViewProviderSketch* viewProvider, int constraintIndex, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::EditTextDialog)
    , sketchView(viewProvider)
    , constrIndex(constraintIndex)
{
    ui->setupUi(this);

    ui->comboBox_font->setMaxVisibleItems(20);

    const Sketcher::SketchObject* sketch = sketchView->getSketchObject();
    const Sketcher::Constraint* constraint = sketch->Constraints[constrIndex];

    // Initialize Text
    ui->lineEdit_text->setText(QString::fromStdString(constraint->getText()));

    // Initialize Font
    populateFontList();
    QString currentFontName = QString::fromStdString(constraint->getFont());
    if (!currentFontName.isEmpty()) {
        int idx = ui->comboBox_font->findText(currentFontName, Qt::MatchFixedString);
        if (idx != -1) {
            ui->comboBox_font->setCurrentIndex(idx);
        }
        else {
            QMessageBox::warning(
                this,
                tr("Font not found"),
                tr("The original font '%1' is not found on your system. A default font has been "
                   "selected.")
                    .arg(currentFontName)
            );
        }
    }
}

EditTextDialog::~EditTextDialog()
{
    delete ui;
}

void EditTextDialog::populateFontList()
{
    fontPathMap = findAvailableFontFiles();
    QStringList fontNames = fontPathMap.keys();
    fontNames.sort(Qt::CaseInsensitive);
    ui->comboBox_font->addItems(fontNames);
}

void EditTextDialog::on_buttonBox_accepted()
{
    const Sketcher::SketchObject* sketch = sketchView->getSketchObject();

    // Get new values from the dialog
    std::string newText = ui->lineEdit_text->text().toStdString();
    QString selectedFontName = ui->comboBox_font->currentText();
    std::string newFontPath = fontPathMap.value(selectedFontName).toStdString();

    const Sketcher::Constraint* constraint = sketch->Constraints[constrIndex];

    // Check if anything changed
    if (newText == constraint->getText()
        && selectedFontName.toStdString() == constraint->getFont()) {
        return;  // Nothing to do
    }

    // Open a command to make the change undo-able
    sketchView->getDocument()->openCommand(
        QT_TRANSLATE_NOOP("Command", "Modify sketch text constraint")
    );

    try {
        // Find if the glyphs were construction geometry to preserve that state.
        // For a dual-line (v2) text the glyphs start at element index 2 (0=width,
        // 1=height lines); a legacy single-line text has glyphs from index 1.
        int firstGlyphIndex = constraint->getTextSchemaVersion() >= 2 ? 2 : 1;
        int firstTextGeoId = constraint->getGeoId(firstGlyphIndex);
        bool isConstruction = false;
        if (firstTextGeoId != Sketcher::GeoEnum::GeoUndef) {
            isConstruction = Sketcher::GeometryFacade::getConstruction(
                sketch->getGeometry(firstTextGeoId)
            );
        }

        // Send the updated call to Python. The isHeight boolean is legacy (it no longer
        // selects a scaling mode); scaling is governed entirely by the constraint state.
        std::string escText = escapeForPython(newText);
        std::string escFont = escapeForPython(newFontPath);
        Gui::cmdAppObjectArgs(
            sketch,
            "setTextAndFont(%i, '%s', '%s', %s, %s)",
            constrIndex,
            escText.c_str(),
            escFont.c_str(),
            "False",
            isConstruction ? "True" : "False"
        );

        sketchView->getDocument()->commitCommand();
    }
    catch (const Base::Exception& e) {
        sketchView->getDocument()->abortCommand();
        Base::Console().error("Failed to modify text constraint: %s\n", e.what());
    }
}
