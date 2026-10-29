#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

// Фиксированный статус, переходов и Observer здесь нет.
enum class Status { Draft, Review, Approved, Published };
enum class EditResult { Applied, EmptyRejected, Locked };

struct Content {
    std::string text = "v1";
    bool dirtyReview = false;
};

const char* name(Status status) {
    const char* names[] = {"Draft", "Review", "Approved", "Published"};
    return names[static_cast<int>(status)];
}

const char* name(EditResult result) {
    const char* names[] = {"applied", "empty_rejected", "locked"};
    return names[static_cast<int>(result)];
}

struct Behavior {
    virtual ~Behavior() = default;
    virtual EditResult edit(Content&, const std::string&) const = 0;
    virtual std::string exportText(const Content&) const = 0;
};

struct DraftBehavior : Behavior {
    EditResult edit(Content& content, const std::string& text) const override {
        if (text.empty()) return EditResult::EmptyRejected;
        content.text = text;
        return EditResult::Applied;
    }
    std::string exportText(const Content& content) const override {
        return "draft:" + content.text;
    }
};

// Правило Review для обеих операций находится в одном классе.
struct ReviewBehavior : Behavior {
    EditResult edit(Content& content, const std::string& text) const override {
        if (text.empty()) return EditResult::EmptyRejected;
        content.text = text;
        content.dirtyReview = true;
        return EditResult::Applied;
    }
    std::string exportText(const Content& content) const override {
        return (content.dirtyReview ? "review-dirty:" : "review:") + content.text;
    }
};

struct ApprovedBehavior : Behavior {
    EditResult edit(Content&, const std::string&) const override {
        return EditResult::Locked;
    }
    std::string exportText(const Content& content) const override {
        return "approved:" + content.text;
    }
};

struct PublishedBehavior : Behavior {
    EditResult edit(Content&, const std::string&) const override {
        return EditResult::Locked;
    }
    std::string exportText(const Content& content) const override {
        return "published:" + content.text;
    }
};

class StateBehaviorDocument {
    Content content_;
    const Behavior* behavior_;

public:
    explicit StateBehaviorDocument(Status status) {
        static const DraftBehavior draft;
        static const ReviewBehavior review;
        static const ApprovedBehavior approved;
        static const PublishedBehavior published;
        const Behavior* behaviors[] = {&draft, &review, &approved, &published};
        behavior_ = behaviors[static_cast<int>(status)];
    }
    EditResult edit(const std::string& text) { return behavior_->edit(content_, text); }
    std::string exportText() const { return behavior_->exportText(content_); }
    const Content& content() const { return content_; }
};

// Другой вариант, отдельные наборы обработчиков по операциям.
// Таблица переходов сама по себе такой раскладки не требует.
namespace operation_handlers {
EditResult editDraft(Content& content, const std::string& text) {
    if (text.empty()) return EditResult::EmptyRejected;
    content.text = text;
    return EditResult::Applied;
}
EditResult editReview(Content& content, const std::string& text) {
    if (text.empty()) return EditResult::EmptyRejected;
    content.text = text;
    content.dirtyReview = true;
    return EditResult::Applied;
}
EditResult editApproved(Content&, const std::string&) { return EditResult::Locked; }
EditResult editPublished(Content&, const std::string&) { return EditResult::Locked; }

std::string exportDraft(const Content& content) { return "draft:" + content.text; }
std::string exportReview(const Content& content) {
    return (content.dirtyReview ? "review-dirty:" : "review:") + content.text;
}
std::string exportApproved(const Content& content) { return "approved:" + content.text; }
std::string exportPublished(const Content& content) { return "published:" + content.text; }

using EditHandler = EditResult (*)(Content&, const std::string&);
using ExportHandler = std::string (*)(const Content&);
const std::array<EditHandler, 4> editHandlers = {
    editDraft, editReview, editApproved, editPublished
};
const std::array<ExportHandler, 4> exportHandlers = {
    exportDraft, exportReview, exportApproved, exportPublished
};
}  // namespace operation_handlers

class OperationHandlerDocument {
    const Status status_;
    Content content_;

public:
    explicit OperationHandlerDocument(Status status) : status_(status) {}
    EditResult edit(const std::string& text) {
        return operation_handlers::editHandlers[static_cast<int>(status_)](content_, text);
    }
    std::string exportText() const {
        return operation_handlers::exportHandlers[static_cast<int>(status_)](content_);
    }
    const Content& content() const { return content_; }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

// Ожидания заданы из правил отдельно от обеих реализаций.
struct Expected {
    Status status;
    EditResult nonemptyResult;
    EditResult emptyResult;
    const char* initialExport;
    const char* editedText;
    bool editedDirty;
    const char* editedExport;
    const char* secondText;
    const char* secondExport;
};

const std::array<Expected, 4> expectations = {{
    {Status::Draft, EditResult::Applied, EditResult::EmptyRejected,
     "draft:v1", "v2", false, "draft:v2", "v3", "draft:v3"},
    {Status::Review, EditResult::Applied, EditResult::EmptyRejected,
     "review:v1", "v2", true, "review-dirty:v2", "v3", "review-dirty:v3"},
    {Status::Approved, EditResult::Locked, EditResult::Locked,
     "approved:v1", "v1", false, "approved:v1", "v1", "approved:v1"},
    {Status::Published, EditResult::Locked, EditResult::Locked,
     "published:v1", "v1", false, "published:v1", "v1", "published:v1"}
}};

template<class Document>
void checkContentAndExport(const Document& document, const char* text,
                           bool dirty, const char* exported) {
    require(document.content().text == text && document.content().dirtyReview == dirty,
            "content mismatch");
    require(document.exportText() == exported, "export mismatch");
    require(document.content().text == text && document.content().dirtyReview == dirty,
            "export changed content");
}

template<class Document>
void run(const char* implementation) {
    for (const auto& expected : expectations) {
        Document document(expected.status);

        checkContentAndExport(document, "v1", false, expected.initialExport);
        const std::string initialExport = document.exportText();

        const EditResult emptyResult = document.edit("");
        require(emptyResult == expected.emptyResult, "initial empty edit mismatch");
        checkContentAndExport(document, "v1", false, expected.initialExport);

        const EditResult editResult = document.edit("v2");
        require(editResult == expected.nonemptyResult, "nonempty edit mismatch");
        checkContentAndExport(document, expected.editedText,
                              expected.editedDirty, expected.editedExport);
        const std::string editedExport = document.exportText();

        require(document.edit("") == expected.emptyResult, "later empty edit mismatch");
        checkContentAndExport(document, expected.editedText,
                              expected.editedDirty, expected.editedExport);

        require(document.edit("v3") == expected.nonemptyResult, "second edit mismatch");
        checkContentAndExport(document, expected.secondText,
                              expected.editedDirty, expected.secondExport);

        std::cout << implementation << " | " << name(expected.status)
                  << " | initial=" << initialExport
                  << " | empty=" << name(emptyResult)
                  << " | edit=" << name(editResult)
                  << " | after_edit=" << editedExport
                  << " | final=" << document.exportText() << '\n';
    }
}

int main() {
    try {
        std::cout << "Fixed-status behavior projection; no workflow transitions.\n";
        run<StateBehaviorDocument>("state");
        run<OperationHandlerDocument>("per-operation handlers");
        std::cout << "PASS: both behavior implementations, all four statuses\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
