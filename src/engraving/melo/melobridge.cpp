/*
 * SPDX-License-Identifier: GPL-3.0-only
 * MuseScore-Studio-CLA-applies
 *
 * JiMStaff Milestone 1 — fork-side wrapper over the melo-musescore-bridge
 * C ABI. See melobridge.h.
 */
#include "melobridge.h"

#include <cmath>

#include "serialization/json.h"
#include "translation.h"

#include "melo_musescore_bridge.h"

using namespace muse;

namespace mu::engraving::melo {
static String callBridge(const String& envelope)
{
    ByteArray utf8 = envelope.toUtf8();
    char* raw = melo_musescore_bridge_request(utf8.constChar());
    if (!raw) {
        return String();
    }
    String out = String::fromUtf8(raw);
    melo_musescore_bridge_free(raw);
    return out;
}

static bool okResult(const String& response, JsonValue& result)
{
    std::string err;
    JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
    if (!err.empty()) {
        return false;
    }
    JsonObject root = doc.rootObject();
    if (!root.value("ok").toBool()) {
        return false;
    }
    result = root.value("result");
    return true;
}

bool available()
{
    // V2 contract (Milestone 2 Phase 4): this fork speaks bridge ABI 2.
    return melo_musescore_bridge_abi_version() == 2;
}

bool validateChordBassSuffix(const String& name)
{
    JsonObject envelope;
    envelope.set("abi", 2);
    envelope.set("op", "chord_bass_suffix_validate");
    envelope.set("name", name);
    JsonValue result;
    return okResult(callBridge(String::fromUtf8(JsonDocument(envelope).toJson())), result);
}

bool validateChordEvidence(const String& evidence, const String& name, String& error, const String& live, const String& offset)
{
    std::string parseError;
    JsonDocument proof = JsonDocument::fromJson(evidence.toUtf8(), &parseError);
    if (!parseError.empty()) {
        error = u"Invalid generated chord evidence JSON";
        return false;
    }
    JsonObject envelope;
    envelope.set("abi", 2);
    envelope.set("op", "chord_evidence_validate");
    envelope.set("evidence", proof.rootObject());
    envelope.set("name", name);
    if (!live.empty()) {
        envelope.set("live", JsonDocument::fromJson(live.toUtf8()).rootArray());
        envelope.set("offset", offset);
    }
    JsonDocument response = JsonDocument::fromJson(callBridge(String::fromUtf8(JsonDocument(envelope).toJson())).toUtf8());
    if (response.rootObject().value("ok").toBool()) {
        return true;
    }
    error = response.rootObject().value("error").toString();
    if (error.empty()) {
        error = u"Kernel could not validate generated chord evidence";
    }
    return false;
}

bool validateState(const String& stateJson, String& error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"validate\",\"state\":%1}").arg(stateJson);
    const String response = callBridge(envelope);
    std::string err;
    JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
    if (!err.empty()) {
        error = mtrc("engraving", "bridge returned no JSON");
        return false;
    }
    JsonObject root = doc.rootObject();
    if (root.value("ok").toBool()) {
        return true;
    }
    error = root.value("error").toString();
    return false;
}

static bool canonicalResult(const String& envelope, String& value, String& error)
{
    std::string parseError;
    const JsonDocument response = JsonDocument::fromJson(
        callBridge(envelope).toUtf8(), &parseError);
    const JsonObject root = response.rootObject();
    if (!parseError.empty() || !root.value("ok").toBool() || !root.value("result").isString()) {
        error = root.value("error").toString();
        if (error.isEmpty()) {
            error = mtrc("engraving", "The Kernel returned no canonical reference result.");
        }
        return false;
    }
    value = root.value("result").toString();
    return true;
}

bool defaultReferenceTimeline(String& timeline, String& error)
{
    return canonicalResult(u"{\"abi\":2,\"op\":\"default_reference_timeline\"}", timeline, error);
}

bool defaultStaffConfiguration(String& configuration, String& error)
{
    return canonicalResult(u"{\"abi\":2,\"op\":\"default_staff_configuration\"}", configuration, error);
}

bool staffRequest(const String& configurations, const String& timeline, const String& at, String& request, String& error)
{
    // Preserve Kernel JSON verbatim: the generic host serializer rounds
    // fractional values, including the exact 7-tet generator, to six places.
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"staff_request\",\"configurations\":%1,\"reference_timeline\":%2,\"at\":%3}")
                           .arg(configurations).arg(timeline).arg(at), request, error);
}

bool staffConfiguration(const String& request, String& configuration, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"staff_configuration\",\"state\":%1}").arg(request), configuration, error);
}

bool staffRequestAt(const String& source, const String& curves, const String& at, String& request, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"staff_request_at\",\"state\":%1,\"curves\":%2,\"at\":%3}")
                           .arg(source).arg(curves).arg(at), request, error);
}

bool validateStaffContext(const String& request, const String& expected, String& error)
{
    const String envelope = String(u"{\"abi\":2,\"op\":\"validate_staff_context\",\"state\":%1,\"expected\":%2}")
                            .arg(request).arg(expected);
    const JsonObject response = JsonDocument::fromJson(callBridge(envelope).toUtf8()).rootObject();
    if (response.value("ok").toBool()) {
        return true;
    }
    error = response.value("error").toString();
    if (error.isEmpty()) {
        error = mtrc("engraving", "The Kernel could not validate the derived staff context.");
    }
    return false;
}

bool inheritStaffConfiguration(const String& source, const String& local, String& configuration, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"inherit_staff_configuration\",\"state\":%1,\"local\":%2}")
                           .arg(source).arg(local), configuration, error);
}

bool storedStaffConfiguration(const String& stored, String& configuration, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"stored_staff_configuration\",\"state\":%1}").arg(stored), configuration, error);
}

bool musicxmlReferenceV5Xml(const String& timeline, String& xml, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"musicxml_reference_v5_xml\",\"reference_timeline\":%1}").arg(timeline), xml, error);
}

bool musicxmlConfigurationV5Xml(const String& request, int staffNumber, bool shared, String& xml, String& error)
{
    String configuration;
    if (!staffConfiguration(request, configuration, error)) {
        return false;
    }
    String fields = shared ? u",\"shared\":true" : String();
    if (staffNumber > 0) {
        fields += String(u",\"staff_number\":%1").arg(staffNumber);
    }
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"musicxml_configuration_v5_xml\",\"state\":%1%2}")
                           .arg(configuration).arg(fields), xml, error);
}

static bool canonicalXmlInput(const char* operation, const String& xml, String& output, String& error)
{
    JsonObject envelope;
    envelope.set("abi", 2);
    envelope.set("op", String::fromAscii(operation));
    envelope.set("xml", xml);
    return canonicalResult(String::fromUtf8(JsonDocument(envelope).toJson(JsonDocument::Format::Compact)), output, error);
}

bool musicxmlReferenceV5Json(const String& xml, String& timeline, String& error)
{
    return canonicalXmlInput("musicxml_reference_v5_json", xml, timeline, error);
}

bool musicxmlConfigurationV5Json(const String& xml, String& configuration, String& error)
{
    return canonicalXmlInput("musicxml_configuration_v5_json", xml, configuration, error);
}

bool editHeaderPitch(const String& request, const String& role, int periodIndex, const String& pitch, String& timeline, String& error)
{
    JsonObject envelope;
    envelope.set("abi", 2);
    envelope.set("op", "header_pitch_edit");
    envelope.set("role", role);
    envelope.set("period_index", periodIndex);
    envelope.set("pitch", pitch);
    const String fields = String::fromUtf8(JsonDocument(envelope).toJson(JsonDocument::Format::Compact));
    return canonicalResult(fields.left(fields.size() - 1) + u",\"state\":" + request + u"}", timeline, error);
}

bool editRelativeKey(const String& request, const String& at, const String& interval, String& timeline, String& error)
{
    return canonicalResult(String(u"{\"abi\":2,\"op\":\"relative_key_edit\",\"state\":%1,\"at\":%2,\"interval\":%3}")
                           .arg(request).arg(at).arg(interval), timeline, error);
}

bool relativeKeyEditor(const String& source, const String& destination, const String& at,
                       const String* expression, RelativeKeyEditor& result, String& error)
{
    JsonObject envelope;
    envelope.set("abi", 2);
    envelope.set("op", "relative_key_editor");
    if (expression) {
        envelope.set("expression", *expression);
    }
    const String fields = String::fromUtf8(JsonDocument(envelope).toJson(JsonDocument::Format::Compact));
    String response;
    if (!canonicalResult(fields.left(fields.size() - 1) + u",\"source_state\":" + source
                         + u",\"state\":" + destination + u",\"at\":" + at + u"}", response, error)) {
        return false;
    }
    const JsonObject root = JsonDocument::fromJson(response.toUtf8()).rootObject();
    const JsonObject interval = root.value("interval").toObject();
    result.expression = root.value("expression").toString();
    result.interval = String(u"{\"nPer\":%1,\"nGen\":%2}").arg(interval.value("nPer").toInt()).arg(interval.value("nGen").toInt());
    result.sourceState = root.value("source_state").toString();
    result.destinationState = root.value("destination_state").toString();
    result.exists = root.value("exists").toBool();
    result.direction = root.value("indicator").toObject().value("key_change").toObject().value("direction").toString();
    return true;
}

bool headerPitchHit(const String& request, int periodIndex, const RectF& ink, const PointF& point)
{
    const String geometry = String(u"{\"ink\":[%1,%2,%3,%4],\"point\":[%5,%6]}")
                            .arg(String::number(ink.x(), 17)).arg(String::number(ink.y(), 17))
                            .arg(String::number(ink.width(), 17)).arg(String::number(ink.height(), 17))
                            .arg(String::number(point.x(), 17)).arg(String::number(point.y(), 17));
    const String envelope
        = String(
              u"{\"abi\":2,\"op\":\"header_pitch_hit\",\"role\":\"staff-header-tonic-pitch\",\"state\":%1,\"period_index\":%2,\"geometry\":%3}")
          .arg(request).arg(periodIndex).arg(geometry);
    JsonValue result;
    return okResult(callBridge(envelope), result) && result.isBool() && result.toBool();
}

bool noteCentsAboveExtentLower(const String& stateJson, int nPer, int nGen, double& cents)
{
    // The staff-frame projection is Kernel-owned end to end: one op, no
    // fork-side ordinate/register/anchor arithmetic.
    String envelope = String(u"{\"abi\":2,\"op\":\"note_cents_above_extent_lower\",\"state\":%1,\"nPer\":%2,\"nGen\":%3}")
                      .arg(stateJson).arg(nPer).arg(nGen);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    cents = result.toDouble();
    return true;
}

static bool updatedStateResult(const String& envelope, String& updatedState)
{
    JsonValue result;
    if (!okResult(callBridge(envelope), result) || !result.isString()) {
        return false;
    }
    updatedState = result.toString();
    return !updatedState.isEmpty();
}

bool widenExtent(const String& stateJson, int nPer, int nGen, String& updatedState)
{
    return updatedStateResult(
        String(u"{\"abi\":2,\"op\":\"widen_extent\",\"state\":%1,\"nPer\":%2,\"nGen\":%3}")
        .arg(stateJson).arg(nPer).arg(nGen), updatedState);
}

bool fitExtent(const String& stateJson, const String& melodyJson, String& updatedState)
{
    return updatedStateResult(
        String(u"{\"abi\":2,\"op\":\"fit_extent\",\"state\":%1,\"melody\":%2}")
        .arg(stateJson).arg(melodyJson), updatedState);
}

bool defaultVocalExtent(const String& stateJson, int lowKey, int highKey, const char* role, String& updatedState)
{
    return updatedStateResult(
        String(u"{\"abi\":2,\"op\":\"default_vocal_extent\",\"state\":%1,\"low_key\":%2,\"high_key\":%3,\"role\":\"%4\"}")
        .arg(stateJson).arg(lowKey).arg(highKey).arg(String::fromAscii(role)), updatedState);
}

bool defaultInstrumentExtent(const String& stateJson, int lowKey, int highKey, String& updatedState)
{
    return updatedStateResult(
        String(u"{\"abi\":2,\"op\":\"default_instrument_extent\",\"state\":%1,\"low_key\":%2,\"high_key\":%3}")
        .arg(stateJson).arg(lowKey).arg(highKey), updatedState);
}

bool retuneGenerator(const String& stateJson, double generatorCents, String& updatedState)
{
    return updatedStateResult(
        String(u"{\"abi\":2,\"op\":\"retune_generator\",\"state\":%1,\"generator_cents\":%2}")
        .arg(stateJson).arg(String::number(generatorCents, 12)), updatedState);
}

bool noteheadToken(const String& stateJson, int nGen, String& token)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"notehead_class\",\"state\":%1,\"nGen\":%2}")
                      .arg(stateJson).arg(nGen);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    token = result.toString();
    return true;
}

bool tonicCentsAboveDo(const String& stateJson, double& cents)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"tonic_cents_above_do\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    cents = result.toDouble();
    return true;
}

bool periodicOrigins(const String& stateJson, PeriodicOrigins& out)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"periodic_origins\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    JsonObject object = result.toObject();
    out.doCentsAboveExtentLower = object.value("do_cents_above_extent_lower").toDouble();
    out.tonicCentsAboveExtentLower = object.value("tonic_cents_above_extent_lower").toDouble();
    return true;
}

bool staffMetrics(const String& stateJson, double& generatorCents, double& periodCents)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"staff_metrics\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    generatorCents = o.value("generator_cents").toDouble();
    periodCents = o.value("period_cents").toDouble();
    return true;
}

bool generatorRange(double& minCents, double& maxCents)
{
    JsonValue result;
    if (!okResult(callBridge(String(u"{\"abi\":2,\"op\":\"generator_range\"}")), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    minCents = o.value("min_cents").toDouble();
    maxCents = o.value("max_cents").toDouble();
    return true;
}

bool toneDiamondSettings(std::vector<ToneDiamondSetting>& settings, uint32_t& generatorParamId,
                         uint32_t& xParamId, uint32_t& yParamId)
{
    JsonValue result;
    if (!okResult(callBridge(String(u"{\"abi\":2,\"op\":\"tone_diamond_settings\"}")), result)
        || !result.isObject()) {
        return false;
    }
    const JsonObject root = result.toObject();
    generatorParamId = uint32_t(root.value("generator_param_id").toInt());
    xParamId = uint32_t(root.value("x_param_id").toInt());
    yParamId = uint32_t(root.value("y_param_id").toInt());
    settings.clear();
    const JsonArray settingsArray = root.value("settings").toArray();
    for (size_t i = 0; i < settingsArray.size(); ++i) {
        const JsonValue value = settingsArray.at(i);
        const JsonObject object = value.toObject();
        const JsonObject point = object.value("point").toObject();
        ToneDiamondSetting setting;
        setting.id = object.value("id").toString();
        setting.label = object.value("label").toString();
        setting.x = point.value("x").toDouble();
        setting.y = point.value("y").toDouble();
        if (setting.id.isEmpty() || setting.label.isEmpty()) {
            settings.clear();
            return false;
        }
        settings.push_back(std::move(setting));
    }
    return !settings.empty();
}

bool labelLegibilityRange(double& minCents, double& maxCents)
{
    JsonValue result;
    if (!okResult(callBridge(String(u"{\"abi\":2,\"op\":\"label_legibility_range\"}")), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    minCents = o.value("min_cents").toDouble();
    maxCents = o.value("max_cents").toDouble();
    return true;
}

bool scaleDotLabels(const String& stateJson, std::vector<LabeledDotStack>& stacks)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"scale_dot_labels\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    double generatorCents = 0.0;
    double periodCents = 0.0;
    if (!staffMetrics(stateJson, generatorCents, periodCents)) {
        return false;
    }
    stacks.clear();
    JsonArray array = result.toArray();
    for (size_t i = 0; i < array.size(); ++i) {
        JsonObject stack = array.at(i).toObject();
        LabeledDotStack out;
        out.cents = stack.value("ordinate").toDouble() * periodCents;
        JsonArray members = stack.value("members").toArray();
        for (size_t j = 0; j < members.size(); ++j) {
            JsonObject member = members.at(j).toObject();
            out.members.push_back({ member.value("nGen").toInt(),
                                    member.value("label").toString() });
        }
        stacks.push_back(out);
    }
    return true;
}

bool jiLines(const String& stateJson, std::vector<JiLine>& lines)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"ji_lines\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    lines.clear();
    JsonArray array = result.toArray();
    for (size_t i = 0; i < array.size(); ++i) {
        JsonObject line = array.at(i).toObject();
        lines.push_back({ line.value("cents").toDouble(),
                          line.value("limit").toInt(),
                          line.value("visible").toBool() });
    }
    return true;
}

static ChangePoint readPoint(const JsonObject& o)
{
    ChangePoint p;
    p.nGen = o.value("nGen").toInt();
    p.label = o.value("label").toString();
    p.ordinate = o.value("ordinate").toDouble();
    p.periodOffset = o.value("period_offset").toInt();
    p.noteheadToken = o.value("notehead_token").toString();
    return p;
}

static bool stringResult(const String& response, String& out, String* error)
{
    std::string err;
    JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
    if (!err.empty()) {
        if (error) {
            *error = mtrc("engraving", "bridge returned no JSON");
        }
        return false;
    }
    JsonObject root = doc.rootObject();
    if (!root.value("ok").toBool()) {
        if (error) {
            *error = root.value("error").toString();
        }
        return false;
    }
    if (!root.value("result").isString()) {
        if (error) {
            *error = u"bridge result is not a string";
        }
        return false;
    }
    out = root.value("result").toString();
    return true;
}

bool tonicAmbitForMelody(const String& stateJson, const String& melodyJson, String& token, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"tonic_ambit\",\"state\":%1,\"melody\":%2}")
                      .arg(stateJson).arg(melodyJson);
    if (!stringResult(callBridge(envelope), token, error)) {
        return false;
    }
    if (token != u"tonic-bounded" && token != u"tonic-centered") {
        if (error) {
            *error = u"Kernel returned an unknown tonic-ambit token: " + token;
        }
        return false;
    }
    return true;
}

bool songwideTonicAmbit(const String& spansJson, String& token, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"songwide_tonic_ambit\",\"spans\":%1}").arg(spansJson);
    if (!stringResult(callBridge(envelope), token, error)) {
        return false;
    }
    return token == u"tonic-bounded" || token == u"tonic-centered";
}

bool sameReference(const String& stateJson, const String& otherStateJson, bool& same, String* error)
{
    const String envelope = String(u"{\"abi\":2,\"op\":\"same_reference\",\"state\":%1,\"other_state\":%2}")
                            .arg(stateJson).arg(otherStateJson);
    const String response = callBridge(envelope);
    JsonValue result;
    if (!okResult(response, result) || !result.isBool()) {
        if (error) {
            std::string parseError;
            const JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &parseError);
            *error = parseError.empty() ? doc.rootObject().value("error").toString() : mtrc("engraving", "bridge returned no JSON");
        }
        return false;
    }
    same = result.toBool();
    return true;
}

static bool readSoundingPitch(const String& response, SoundingPitch& out, String* error)
{
    JsonValue result;
    if (!okResult(response, result)) {
        if (error) {
            std::string err;
            JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
            *error = err.empty() ? doc.rootObject().value("error").toString() : mtrc("engraving", "bridge returned no JSON");
        }
        return false;
    }
    JsonObject o = result.toObject();
    // Every field the transport relies on must be present and well-typed;
    // a malformed answer is a failure, never a default pitch.
    for (const char* key : { "nPer", "nGen", "frequency_hz", "midi_key", "cents_offset", "step", "alter", "octave",
                             "reference_key_number", "reference_frequency_hz", "anchor" }) {
        if (!o.contains(key)) {
            if (error) {
                *error = mtrc("engraving", "note_sounding_pitch answer lacks %1").arg(String::fromAscii(key));
            }
            return false;
        }
    }
    out.nPer = o.value("nPer").toInt();
    out.nGen = o.value("nGen").toInt();
    out.frequencyHz = o.value("frequency_hz").toDouble();
    out.midiKey = o.value("midi_key").toInt();
    out.centsOffset = o.value("cents_offset").toDouble();
    String step = o.value("step").toString();
    out.step = step.isEmpty() ? '\0' : step.at(0).toAscii();
    out.alter = o.value("alter").toInt();
    out.octave = o.value("octave").toInt();
    out.referenceKeyNumber = o.value("reference_key_number").toInt();
    out.referenceFrequencyHz = o.value("reference_frequency_hz").toDouble();
    out.anchor = o.value("anchor").toString();
    if (out.midiKey < 0 || out.midiKey > 127 || !(out.frequencyHz > 0.0)
        || std::abs(out.centsOffset) > 50.0 + 1e-9 || step.size() != 1
        || String(u"CDEFGAB").indexOf(step.at(0)) == muse::nidx) {
        if (error) {
            *error = mtrc("engraving", "note_sounding_pitch answer out of range");
        }
        return false;
    }
    return true;
}

bool noteSoundingPitch(const String& stateJson, int nPer, int nGen, SoundingPitch& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"note_sounding_pitch\",\"state\":%1,\"nPer\":%2,\"nGen\":%3}")
                      .arg(stateJson).arg(nPer).arg(nGen);
    return readSoundingPitch(callBridge(envelope), out, error);
}

bool vst3ProfileTransaction(const String& stateJson, uint32_t slot, uint32_t generation, uint32_t sampleOffset,
                            mpe::DynamicTonalityProfileEvent& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"vst3_profile_transaction\",\"state\":%1,"
                             "\"slot\":%2,\"generation\":%3,\"sample_offset\":%4}")
                      .arg(stateJson)
                      .arg(static_cast<int64_t>(slot))
                      .arg(static_cast<int64_t>(generation))
                      .arg(static_cast<int64_t>(sampleOffset));
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        if (error) {
            *error = u"Kernel rejected the VST3 tuning profile";
        }
        return false;
    }
    JsonArray points = result.toArray();
    if (points.size() != mpe::DynamicTonalityProfileEvent::POINT_COUNT) {
        if (error) {
            *error = u"Kernel returned an incomplete VST3 tuning profile";
        }
        return false;
    }
    for (size_t i = 0; i < points.size(); ++i) {
        JsonObject point = points.at(i).toObject();
        out.points[i].paramId = static_cast<uint32_t>(point.value("param_id").toInt());
        out.points[i].normalized = point.value("normalized").toDouble();
    }
    return true;
}

static void readTonicPitchLabel(const JsonObject& o, TonicPitchLabel& out);

bool tonicPitchLabel(const String& stateJson, TonicPitchLabel& out)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"tonic_pitch_label\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    readTonicPitchLabel(result.toObject(), out);
    return !out.label.isEmpty();
}

bool tonicPitchLabelInPeriod(const String& stateJson, int periodIndex, TonicPitchLabel& out)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"tonic_pitch_label\",\"state\":%1,\"period_index\":%2}")
                      .arg(stateJson).arg(periodIndex);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    readTonicPitchLabel(result.toObject(), out);
    return !out.label.isEmpty();
}

static void readTonicPitchLabel(const JsonObject& o, TonicPitchLabel& out)
{
    out.label = o.value("label").toString();
    out.keyNumber = o.value("key_number").toInt();
    out.nPer = o.value("nPer").toInt();
    out.nGen = o.value("nGen").toInt();
}

bool changeIndicator(const String& oldStateJson, const String& newStateJson, ChangeIndicator& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"change_indicator\",\"old_state\":%1,\"new_state\":%2}")
                      .arg(oldStateJson).arg(newStateJson);
    const String response = callBridge(envelope);
    JsonValue result;
    if (!okResult(response, result)) {
        if (error) {
            std::string err;
            JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
            *error = err.empty() ? doc.rootObject().value("error").toString() : mtrc("engraving", "bridge returned no JSON");
        }
        return false;
    }
    out = ChangeIndicator();
    JsonObject o = result.toObject();
    JsonArray kinds = o.value("kinds").toArray();
    for (size_t i = 0; i < kinds.size(); ++i) {
        out.kinds.push_back(kinds.at(i).toString());
    }
    JsonObject terrain = o.value("terrain").toObject();
    JsonArray stacks = terrain.value("dot_stacks").toArray();
    for (size_t i = 0; i < stacks.size(); ++i) {
        JsonObject s = stacks.at(i).toObject();
        ChangeStack stack;
        stack.ordinate = s.value("ordinate").toDouble();
        stack.periodOffset = s.value("period_offset").toInt();
        JsonArray members = s.value("members").toArray();
        for (size_t j = 0; j < members.size(); ++j) {
            stack.members.push_back(readPoint(members.at(j).toObject()));
        }
        out.dotStacks.push_back(stack);
    }
    JsonArray tonics = terrain.value("tonic_indicators").toArray();
    for (size_t i = 0; i < tonics.size(); ++i) {
        out.tonicIndicators.push_back(readPoint(tonics.at(i).toObject()));
    }
    JsonArray arrows = terrain.value("arrows").toArray();
    for (size_t i = 0; i < arrows.size(); ++i) {
        JsonObject a = arrows.at(i).toObject();
        ChangeArrow arrow;
        arrow.kind = a.value("kind").toString();
        arrow.from = readPoint(a.value("from").toObject());
        arrow.to = readPoint(a.value("to").toObject());
        arrow.up = a.value("direction").toString() == u"up";
        arrow.trumps = a.value("trumps").toString();
        out.arrows.push_back(arrow);
    }
    return true;
}

bool connectorGlyph(ConnectorGlyph& out)
{
    JsonValue result;
    if (!okResult(callBridge(String(u"{\"abi\":2,\"op\":\"connector_glyph\"}")), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    out.penCents = o.value("pen_cents").toDouble();
    out.headHeightCents = o.value("head_height_cents").toDouble();
    out.headHalfWidthCents = o.value("head_half_width_cents").toDouble();
    return true;
}

static String meloExtraCentsJson(const std::vector<double>& extraCents)
{
    String out = u"[";
    for (size_t i = 0; i < extraCents.size(); ++i) {
        if (i) {
            out += u",";
        }
        // Frame edges may be exact ratios; decimal truncation must not
        // turn an existing boundary into apparent extra coverage.
        out += String::number(extraCents[i], 17);
    }
    return out + u"]";
}

bool frameForMelody(const String& stateJson, const String& melodyJson,
                    const String& extentToken, std::vector<StaveSegment>& segments,
                    const std::vector<double>& extraCents, const String& ratioLineExtentJson, bool retainWrittenExtent)
{
    // Owner rule 2026-08-19 (7b): extra cents the frame must cover ride in
    // the same op's options; without them the envelope is byte-identical
    // to the Milestone-4 request.
    String options = String(u"\"extra_cents\":%1").arg(meloExtraCentsJson(extraCents));
    if (!ratioLineExtentJson.isEmpty()) {
        options += String(u",\"ratio_extent\":%1").arg(ratioLineExtentJson);
    }
    options += String(u",\"retain_written_extent\":%1").arg(String(retainWrittenExtent ? u"true" : u"false"));
    String envelope = extraCents.empty() && ratioLineExtentJson.isEmpty() && !retainWrittenExtent
                      ? String(u"{\"abi\":2,\"op\":\"frame_for_melody\",\"state\":%1,\"melody\":%2,\"declared_extent\":\"%3\"}")
                      .arg(stateJson).arg(melodyJson).arg(extentToken)
                      : String(u"{\"abi\":2,\"op\":\"frame_for_melody\",\"state\":%1,\"melody\":%2,\"declared_extent\":\"%3\","
                               u"\"options\":{%4}}")
                      .arg(stateJson).arg(melodyJson).arg(extentToken).arg(options);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    segments.clear();
    JsonArray array = result.toObject().value("segments").toArray();
    for (size_t i = 0; i < array.size(); ++i) {
        JsonObject seg = array.at(i).toObject();
        segments.push_back({ seg.value("lower_cents").toDouble(),
                             seg.value("upper_cents").toDouble(),
                             seg.value("whole").toBool() });
    }
    return !segments.empty();
}

static bool readFrameBands(const JsonValue& result, FrameBands& out);

bool alignSectionFrames(const String& stateJson, const std::vector<FrameAlignmentSection>& sections,
                        bool preserveGaps, FrameBands& out)
{
    String json = u"[";
    for (size_t i = 0; i < sections.size(); ++i) {
        if (i) {
            json += u",";
        }
        json += String(u"{\"state\":%1,\"segments\":[").arg(sections[i].stateJson);
        for (size_t j = 0; j < sections[i].segments.size(); ++j) {
            if (j) {
                json += u",";
            }
            const StaveSegment& segment = sections[i].segments[j];
            json += String(u"{\"lower_cents\":%1,\"upper_cents\":%2,\"whole\":%3}")
                    .arg(String::number(segment.lowerCents, 17)).arg(String::number(segment.upperCents, 17))
                    .arg(String(segment.whole ? u"true" : u"false"));
        }
        json += u"]}";
    }
    json += u"]";
    JsonValue result;
    return okResult(callBridge(String(u"{\"abi\":2,\"op\":\"align_section_frames\",\"state\":%1,"
                                      u"\"sections\":%2,\"preserve_gaps\":%3}")
                               .arg(stateJson).arg(json).arg(String(preserveGaps ? u"true" : u"false"))), result)
           && readFrameBands(result, out);
}

bool frameBandsForMelody(const String& stateJson, const String& melodyJson,
                         const String& extentToken, bool elideEmptyPeriods, int minBandPeriods,
                         FrameBands& out, const std::vector<double>& extraCents, const String& ratioLineExtentJson,
                         bool retainWrittenExtent)
{
    String options = String(u"\"elide_empty_periods\":%1,\"min_band_periods\":%2,\"extra_cents\":%3")
                     .arg(String(elideEmptyPeriods ? u"true" : u"false"))
                     .arg(minBandPeriods)
                     .arg(meloExtraCentsJson(extraCents));
    if (!ratioLineExtentJson.isEmpty()) {
        options += String(u",\"ratio_extent\":%1").arg(ratioLineExtentJson);
    }
    options += String(u",\"retain_written_extent\":%1").arg(String(retainWrittenExtent ? u"true" : u"false"));
    String envelope = String(
        u"{\"abi\":2,\"op\":\"frame_bands_for_melody\",\"state\":%1,\"melody\":%2,\"declared_extent\":\"%3\","
        u"\"options\":{%4}}")
                      .arg(stateJson).arg(melodyJson).arg(extentToken).arg(options);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    if (o.value("schema").toString() != u"jims.frame-bands.v1") {
        return false;
    }
    return readFrameBands(result, out);
}

static bool readFrameBands(const JsonValue& result, FrameBands& out)
{
    JsonObject o = result.toObject();
    out = FrameBands();
    out.omittedPeriodCount = o.value("omitted_period_count").toInt();
    JsonArray bands = o.value("bands").toArray();
    for (size_t i = 0; i < bands.size(); ++i) {
        JsonObject b = bands.at(i).toObject();
        FrameBand band;
        JsonArray segs = b.value("segments").toArray();
        for (size_t j = 0; j < segs.size(); ++j) {
            JsonObject seg = segs.at(j).toObject();
            band.segments.push_back({ seg.value("lower_cents").toDouble(),
                                      seg.value("upper_cents").toDouble(),
                                      seg.value("whole").toBool() });
        }
        band.lowerCents = b.value("lower_cents").toDouble();
        band.upperCents = b.value("upper_cents").toDouble();
        band.lowestPeriodIndex = b.value("lowest_period_index").toInt();
        band.highestPeriodIndex = b.value("highest_period_index").toInt();
        band.labelPeriodIndex = b.value("label_period_index").toInt();
        readTonicPitchLabel(b.value("tonic_label").toObject(), band.tonicLabel);
        if (band.segments.empty()) {
            return false;
        }
        out.bands.push_back(band);
    }
    return !out.bands.empty();
}

bool nearestPitch(const String& stateJson, double targetCents,
                  bool hasCurrent, int currentNPer, int currentNGen, PitchHit& hit, const String& noteheadClass)
{
    String current = hasCurrent
                     ? String(u"{\"nPer\":%1,\"nGen\":%2}").arg(currentNPer).arg(currentNGen)
                     : String(u"null");
    const String shape = noteheadClass.isEmpty() ? String(u"null") : String(u"\"%1\"").arg(noteheadClass);
    String envelope = String(u"{\"abi\":2,\"op\":\"nearest_pitch\",\"state\":%1,\"target_cents\":%2,\"current\":%3,\"notehead_class\":%4}")
                      .arg(stateJson).arg(String::number(targetCents, 6)).arg(current).arg(shape);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    hit.nPer = o.value("nPer").toInt();
    hit.nGen = o.value("nGen").toInt();
    hit.centsAboveExtentLower = o.value("cents_above_extent_lower").toDouble();
    JsonObject cp = o.value("compatibility_pitch").toObject();
    muse::String step = cp.value("step").toString();
    hit.step = step.isEmpty() ? 'C' : step.at(0).toAscii();
    hit.alter = cp.value("alter").toInt();
    hit.octave = cp.value("octave").toInt();
    return true;
}

static void readPitchHit(const JsonObject& o, PitchHit& hit)
{
    hit.nPer = o.value("nPer").toInt();
    hit.nGen = o.value("nGen").toInt();
    hit.centsAboveExtentLower = o.value("cents_above_extent_lower").toDouble();
    JsonObject cp = o.value("compatibility_pitch").toObject();
    muse::String step = cp.value("step").toString();
    hit.step = step.isEmpty() ? 'C' : step.at(0).toAscii();
    hit.alter = cp.value("alter").toInt();
    hit.octave = cp.value("octave").toInt();
}

bool stepPitch(const String& stateJson, int currentNPer, int currentNGen,
               bool up, const char* domain, PitchHit& hit)
{
    String envelope = String(
        u"{\"abi\":2,\"op\":\"step_pitch\",\"state\":%1,\"current\":{\"nPer\":%2,\"nGen\":%3},\"up\":%4,\"domain\":\"%5\"}")
                      .arg(stateJson).arg(currentNPer).arg(currentNGen)
                      .arg(String(up ? u"true" : u"false")).arg(String::fromAscii(domain));
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    readPitchHit(result.toObject(), hit);
    return true;
}

static StateChangeOption readOption(const JsonObject& o)
{
    StateChangeOption opt;
    opt.id = o.value("id").toString();
    opt.label = o.value("label").toString();
    opt.hasNGen = o.contains("nGen");
    opt.nGen = o.value("nGen").toInt();
    opt.nPer = o.value("nPer").toInt();
    opt.current = o.value("current").toBool();
    if (o.contains("members") && o.value("members").isArray()) {
        JsonArray members = o.value("members").toArray();
        for (size_t i = 0; i < members.size(); ++i) {
            opt.memberLabels.push_back(members.at(i).toObject().value("label").toString());
        }
    }
    return opt;
}

bool stateChangeOptions(const String& stateJson, StateChangeOptions& options)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"state_change_options\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    JsonObject o = result.toObject();
    options = StateChangeOptions();
    auto readList = [&](const char* key, std::vector<StateChangeOption>& out) {
        if (!o.contains(key) || !o.value(key).isArray()) {
            return;
        }
        JsonArray arr = o.value(key).toArray();
        for (size_t i = 0; i < arr.size(); ++i) {
            out.push_back(readOption(arr.at(i).toObject()));
        }
    };
    readList("tonics", options.tonics);
    readList("rotations", options.rotations);
    readList("cycles", options.cycles);
    return true;
}

bool applyStateChange(const String& stateJson, const String& choiceId, String& newStateJson, String& error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"apply_state_change\",\"state\":%1,\"choice\":\"%2\"}")
                      .arg(stateJson).arg(choiceId);
    const String response = callBridge(envelope);
    std::string err;
    JsonDocument doc = JsonDocument::fromJson(response.toUtf8(), &err);
    if (!err.empty()) {
        error = mtrc("engraving", "bridge returned no JSON");
        return false;
    }
    JsonObject root = doc.rootObject();
    if (!root.value("ok").toBool()) {
        error = root.value("error").toString();
        return false;
    }
    // The Kernel's own JSON text IS the state: slice it out of the envelope
    // verbatim ({"ok":true,"result":{...}}) rather than re-serializing
    // through a JSON library that would reorder keys.
    static const String marker(u"\"result\":");
    const size_t at = response.indexOf(marker);
    if (at == muse::nidx || !response.endsWith(u"}")) {
        error = mtrc("engraving", "bridge envelope has no result");
        return false;
    }
    newStateJson = response.mid(at + marker.size(), response.size() - (at + marker.size()) - 1);
    return true;
}

bool entryFromStandardPitch(const String& stateJson, char step, int alter, int octave,
                            SoundingPitch& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"entry_from_standard_pitch\",\"state\":%1,\"step\":\"%2\",\"alter\":%3,\"octave\":%4}")
                      .arg(stateJson).arg(String(muse::Char(step))).arg(alter).arg(octave);
    return readSoundingPitch(callBridge(envelope), out, error);
}

bool reframeNote(const String& sourceState, const String& targetState, int nPer, int nGen,
                 SoundingPitch& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"reframe_note\",\"state\":%1,\"target_state\":%2,\"nPer\":%3,\"nGen\":%4}")
                      .arg(sourceState).arg(targetState).arg(nPer).arg(nGen);
    return readSoundingPitch(callBridge(envelope), out, error);
}

bool noteContinuation(const String& stateJson, double frequencyHz, SoundingPitch& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"note_continuation\",\"state\":%1,\"frequency_hz\":%2}")
                      .arg(stateJson).arg(String::number(frequencyHz, 17));
    return readSoundingPitch(callBridge(envelope), out, error);
}

bool transposeNote(const String& stateJson, int nPer, int nGen, int steps, int keys, SoundingPitch& out, String* error)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"transpose_note\",\"state\":%1,\"nPer\":%2,\"nGen\":%3,\"steps\":%4,\"keys\":%5}")
                      .arg(stateJson).arg(nPer).arg(nGen).arg(steps).arg(keys);
    return readSoundingPitch(callBridge(envelope), out, error);
}

bool scaleDots(const String& stateJson, std::vector<ScaleDotStack>& stacks)
{
    String envelope = String(u"{\"abi\":2,\"op\":\"scale_dots\",\"state\":%1}").arg(stateJson);
    JsonValue result;
    if (!okResult(callBridge(envelope), result)) {
        return false;
    }
    double generatorCents = 0.0;
    double periodCents = 0.0;
    if (!staffMetrics(stateJson, generatorCents, periodCents)) {
        return false;
    }

    stacks.clear();
    JsonArray array = result.toArray();
    for (size_t i = 0; i < array.size(); ++i) {
        JsonObject stack = array.at(i).toObject();
        ScaleDotStack out;
        out.cents = stack.value("ordinate").toDouble() * periodCents;
        JsonArray members = stack.value("front_to_back").toArray();
        for (size_t j = 0; j < members.size(); ++j) {
            out.frontToBack.push_back(members.at(j).toInt());
        }
        stacks.push_back(out);
    }
    return true;
}
}
