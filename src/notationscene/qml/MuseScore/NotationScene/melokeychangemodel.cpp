// SPDX-License-Identifier: GPL-3.0-only
// MuseScore-Studio-CLA-applies
#include "melokeychangemodel.h"
#include "engraving/dom/masterscore.h"
#include "engraving/dom/measure.h"
#include "engraving/dom/staff.h"
#include "engraving/dom/stafflines.h"
#include "engraving/dom/stafftype.h"
#include "engraving/dom/linkedobjects.h"
#include "engraving/melo/melochange.h"
#include "engraving/rendering/score/tdraw.h"
#include "draw/painter.h"
#include "translation.h"
#include <QPainter>

using namespace mu::notation;
using namespace mu::engraving;

MeloKeyChangeModel::MeloKeyChangeModel(QObject* parent)
    : QObject(parent), muse::Contextable(muse::iocCtxForQmlObject(this)) {}
MeloKeyChangeModel::~MeloKeyChangeModel() = default;

bool MeloKeyChangeModel::current(Score*& score)
{
    score = m_notation && m_notation->elements() ? m_notation->elements()->msScore() : nullptr;
    muse::String state;
    if (!m_active || !score || !context() || m_notation != context()->currentNotation()
        || !melo::effectiveState(score, m_staff, score->tick2measure(m_tick), m_tick, state)
        || state != m_expectedState || score->masterScore()->metaTag(melo::REFERENCE_TIMELINE_TAG) != m_expectedTimeline) {
        m_error = muse::qtrc("notation", "The score changed. Reopen the relative key-change editor.");
        return false;
    }
    return true;
}

void MeloKeyChangeModel::load(int staff, int numerator, int denominator, const QString& expectedState, const QString& expectedTimeline)
{
    m_active = false;
    m_preview.reset();
    m_editor = {};
    m_error.clear();
    m_notation = context() ? context()->currentNotation() : nullptr;
    Score* score = m_notation && m_notation->elements() ? m_notation->elements()->msScore() : nullptr;
    if (!score || staff < 0 || size_t(staff) >= score->nstaves() || numerator <= 0 || denominator <= 0) {
        m_error = muse::qtrc("notation", "Select a later musical position for a relative key change.");
        emit changed();
        return;
    }
    m_staff = staff;
    m_tick = Fraction(numerator, denominator);
    m_expectedState = muse::String::fromQString(expectedState);
    m_expectedTimeline = muse::String::fromQString(expectedTimeline);
    m_active = true;
    context()->currentNotationChanged().onNotify(this, [this]() { cancel(); });
    if (m_notation->interaction()) {
        m_notation->interaction()->selectionChanged().onNotify(this, [this]() { cancel(); });
    }
    prepare(nullptr);
}

void MeloKeyChangeModel::prepare(const muse::String* expression)
{
    m_error.clear();
    m_description.clear();
    m_preview.reset();
    if (expression) {
        m_expression = expression->toQString();
    }
    Score* score = nullptr;
    muse::String error;
    if (!current(score)) {
        emit changed();
        return;
    }
    melo::RelativeKeyEditor prepared;
    if (!melo::prepareRelativeKeyEditor(score, m_staff, m_tick, expression, prepared, error)) {
        m_error = error.toQString();
        emit changed();
        return;
    }
    m_editor = prepared;
    if (!expression) {
        m_expression = prepared.expression.toQString();
    }
    Staff* origin = score->staff(m_staff);
    if (!score->isMaster()) {
        origin = nullptr;
        if (score->staff(m_staff)->links()) {
            for (EngravingObject* object : *score->staff(m_staff)->links()) {
                if (object->isStaff() && toStaff(object)->score()->isMaster()) {
                    origin = toStaff(object);
                    break;
                }
            }
        }
    }
    if (!origin) {
        m_error = muse::qtrc("notation", "The selected part has no linked master staff.");
        emit changed();
        return;
    }
    m_previewStaff = int(origin->idx());
    std::unique_ptr<MasterScore> preview(score->masterScore()->clone());
    if (!preview || !melo::changeRelativeKey(preview.get(), m_previewStaff, m_tick, prepared.interval, m_expectedTimeline, error)) {
        m_error = error.isEmpty() ? muse::qtrc("notation", "The score preview could not be prepared.") : error.toQString();
        emit changed();
        return;
    }
    preview->doLayout();
    m_preview = std::move(preview);
    if (prepared.direction == u"up") {
        m_description = muse::qtrc("notation", "The new key is higher by this interval, measured in the preceding tuning.");
    } else if (prepared.direction == u"down") {
        m_description = muse::qtrc("notation", "The new key is lower by this interval, measured in the preceding tuning.");
    } else if (prepared.direction == u"stationary") {
        m_description = muse::qtrc("notation",
                                   "This interval keeps the same sounding pitch in the preceding tuning. Its relative identity is preserved.");
    } else {
        m_description = muse::qtrc("notation", "No relative key change at this position. Other changes stay in place.");
    }
    emit changed();
}

void MeloKeyChangeModel::preview(const QString& expression)
{
    const auto text = muse::String::fromQString(expression);
    prepare(&text);
}

bool MeloKeyChangeModel::commit(const QString& expression)
{
    // Revalidate against the live score, including linked parts, before its first mutation.
    preview(expression);
    Score* score = nullptr;
    if (!valid() || !current(score)) {
        emit changed();
        return false;
    }
    muse::String error;
    if (!melo::changeRelativeKey(score, m_staff, m_tick, m_editor.interval, m_expectedTimeline, error)) {
        m_error = error.toQString();
        emit changed();
        return false;
    }
    m_active = false;
    if (score->masterScore()->metaTag(melo::REFERENCE_TIMELINE_TAG) != m_expectedTimeline) {
        m_notation->notationChanged().notify();
    }
    emit changed();
    return true;
}

void MeloKeyChangeModel::cancel()
{
    m_active = false;
    m_preview.reset();
    emit changed();
    emit cancelled();
}

void MeloKeyChangeModel::paintPreview(QPainter* qp, qreal width, qreal height) const
{
    if (!valid()) {
        return;
    }
    Measure* measure = m_preview->tick2measure(m_tick);
    StaffLines* lines = measure ? measure->staffLines(m_previewStaff) : nullptr;
    const StaffType* type = m_preview->staff(m_previewStaff)->staffType(m_tick);
    melo::ChangeIndicator indicator;
    if (!lines || !type || !melo::changeIndicator(m_editor.sourceState, m_editor.destinationState, indicator)) {
        return;
    }
    const auto& view = type->meloFrameView(m_preview.get(), m_previewStaff, measure->system());
    if (view.empty()) {
        return;
    }
    const auto geometry = melo::changeTerrainGeometry(type, lines->spatium(), m_preview->style().defaultSpatium(), indicator);
    const double top = lines->pos().y() + type->meloYFromCents(view.topCents(), view) * lines->spatium();
    const double bottom = lines->pos().y() + type->meloYFromCents(view.bottomCents(), view) * lines->spatium();
    const double padding = 3.0 * lines->spatium();
    const double w = geometry.changeTerrainWidth + 2.0 * padding;
    const double h = std::abs(bottom - top) + 2.0 * padding;
    const double scale = std::min(width / w, height / h);
    qp->save();
    qp->translate((width - w * scale) / 2.0, (height - h * scale) / 2.0);
    qp->scale(scale, scale);
    qp->translate(padding, padding - top);
    {
        muse::draw::Painter painter(qp, "relative-key-preview");
        rendering::PaintOptions options;
        options.isPrinting = true;
        rendering::score::TDraw::drawMeloChangeTerrain(lines, &painter, options, indicator, type, type, 0,
                                                       rendering::score::TDraw::ChangePlacement::MID_BAR);
    }
    qp->restore();
}

void MeloKeyChangePreview::setModel(MeloKeyChangeModel* model)
{
    if (m_model == model) {
        return;
    }
    if (m_model) {
        disconnect(m_model, nullptr, this, nullptr);
    }
    m_model = model;
    if (m_model) {
        connect(m_model, &MeloKeyChangeModel::changed, this, [this]() {
            update();
        });
    }
    emit modelChanged();
    update();
}

void MeloKeyChangePreview::paint(QPainter* painter)
{
    painter->fillRect(boundingRect(), Qt::white);
    if (m_model) {
        m_model->paintPreview(painter, width(), height());
    }
}
