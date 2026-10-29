#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

enum Status { Draft, Review, Approved, Published };
enum Event { Submit, Approve, Reject, Publish };
enum Result { accepted, rejected, diagnostic_error, required_error, unexpected_error };
struct DiagnosticError {};
struct RequiredError {};

const char* name(Status s) {
    const char* names[] = {"Draft", "Review", "Approved", "Published"};
    return names[s];
}

const char* name(Result r) {
    const char* names[] = {
        "accepted", "rejected", "diagnostic_error", "required_error", "unexpected_error"
    };
    return names[r];
}

// Первый вариант, переход выбирает объект текущего состояния.
struct State {
    virtual ~State() = default;
    virtual Status status() const = 0;
    virtual const State* next(Event) const = 0;
};
struct DraftState : State {
    Status status() const override { return Draft; }
    const State* next(Event) const override;
};
struct ReviewState : State {
    Status status() const override { return Review; }
    const State* next(Event) const override;
};
struct ApprovedState : State {
    Status status() const override { return Approved; }
    const State* next(Event) const override;
};
struct PublishedState : State {
    Status status() const override { return Published; }
    const State* next(Event) const override { return nullptr; }
};
const DraftState draft;
const ReviewState review;
const ApprovedState approved;
const PublishedState published;
const State* DraftState::next(Event e) const { return e == Submit ? &review : nullptr; }
const State* ReviewState::next(Event e) const {
    if (e == Approve) return &approved;
    if (e == Reject) return &draft;
    return nullptr;
}
const State* ApprovedState::next(Event e) const { return e == Publish ? &published : nullptr; }

class StateMachine {
    const State* current_;
public:
    explicit StateMachine(Status initial) {
        const State* states[] = {&draft, &review, &approved, &published};
        current_ = states[initial];
    }
    Status status() const { return current_->status(); }
    bool apply(Event e) {
        const State* next = current_->next(e);
        if (!next) return false;
        current_ = next;
        return true;
    }
};

// Второй вариант, собственная таблица без обращения к классам State.
class TableMachine {
    Status current_;
public:
    explicit TableMachine(Status initial) : current_(initial) {}
    Status status() const { return current_; }
    bool apply(Event e) {
        struct Rule { Status from; Event event; Status to; };
        const Rule rules[] = {{Draft, Submit, Review}, {Review, Approve, Approved},
                              {Review, Reject, Draft}, {Approved, Publish, Published}};
        for (const auto& rule : rules) {
            if (rule.from == current_ && rule.event == e) {
                current_ = rule.to;
                return true;
            }
        }
        return false;
    }
};

template<class Machine>
class Document {
    struct Subscriber {
        std::function<void(Status)> update;
        bool optional_diagnostic;
    };
    Machine machine_;
    bool isolate_;
    std::vector<Subscriber> subscribers_;

public:
    int notifications = 0;
    int diagnostic_errors = 0;

    Document(Status initial, bool isolate) : machine_(initial), isolate_(isolate) {}
    Status status() const { return machine_.status(); }
    void attach(std::function<void(Status)> update, bool optional_diagnostic = false) {
        subscribers_.push_back({update, optional_diagnostic});
    }

    bool apply(Event event) {
        if (!machine_.apply(event)) return false;
        // Ошибка подписчика уже выполненный переход не откатывает.
        ++notifications;
        for (const auto& subscriber : subscribers_) {
            try {
                subscriber.update(status());
            } catch (const DiagnosticError&) {
                if (!isolate_ || !subscriber.optional_diagnostic) throw;
                ++diagnostic_errors;
            }
        }
        return true;
    }
};

// Этот обработчик находится снаружи рассылки и не продолжает ее после ошибки.
template<class Machine>
Result attempt(Document<Machine>& document, Event event) {
    try {
        return document.apply(event) ? accepted : rejected;
    } catch (const DiagnosticError&) {
        return diagnostic_error;
    } catch (const RequiredError&) {
        return required_error;
    } catch (const std::logic_error&) {
        return unexpected_error;
    }
}

template<class Machine>
struct Example {
    bool diagnostic_fails = false;
    bool unexpected_diagnostic_error = false;
    bool view_fails = false;
    bool view_throws_diagnostic_error = false;
    Status shown;
    int counter = 0;
    Document<Machine> document;

    Example(bool isolate = false, bool diagnostic_first = true, Status initial = Draft)
        : shown(initial), document(initial, isolate) {
        auto diagnostic = [this](Status) {
            if (unexpected_diagnostic_error) throw std::logic_error("diagnostic bug");
            if (diagnostic_fails) throw DiagnosticError{};
        };
        if (diagnostic_first) document.attach(diagnostic, true);
        document.attach([this](Status status) {
            if (view_throws_diagnostic_error) throw DiagnosticError{};
            if (view_fails) throw RequiredError{};
            shown = status;
        });
        document.attach([this](Status) { ++counter; });
        if (!diagnostic_first) document.attach(diagnostic, true);
    }
};

void check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

template<class Machine>
Result expect(Example<Machine>& x, Event event, Result result,
              Status model, Status view, int counter) {
    const Result actual = attempt(x.document, event);
    check(actual == result, "wrong operation result");
    check(x.document.status() == model, "wrong model status");
    check(x.shown == view, "wrong view status");
    check(x.counter == counter, "wrong counter");
    return actual;
}

template<class Machine>
void show(const char* label, const Example<Machine>& x, Result result) {
    std::cout << label << ": " << name(result) << ", "
              << name(x.document.status()) << '/' << name(x.shown)
              << '/' << x.counter << '\n';
}

template<class Machine>
void run(const char* engine) {
    std::cout << engine << "\n";
    // Ожидания из правил. Document эту матрицу не использует.
    // Строки: Draft, Review, Approved, Published; столбцы: Submit, Approve, Reject, Publish.
    const int expected[4][4] = {{1, -1, -1, -1}, {-1, 2, 0, -1},
                              {-1, -1, -1, 3}, {-1, -1, -1, -1}};
    for (int state = 0; state < 4; ++state) {
        for (int event = 0; event < 4; ++event) {
            Example<Machine> x(false, true, static_cast<Status>(state));
            const bool allowed = expected[state][event] != -1;
            const auto next = static_cast<Status>(allowed ? expected[state][event] : state);
            expect(x, static_cast<Event>(event), allowed ? accepted : rejected,
                   next, next, allowed ? 1 : 0);
            check(x.document.notifications == (allowed ? 1 : 0), "wrong notification count");
        }
    }
    std::cout << "transitions: all 16 state/event pairs passed\n";

    Example<Machine> happy;
    expect(happy, Submit, accepted, Review, Review, 1);
    expect(happy, Approve, accepted, Approved, Approved, 2);
    const auto published = expect(happy, Publish, accepted, Published, Published, 3);
    show("happy path", happy, published);

    Example<Machine> refused;
    expect(refused, Submit, accepted, Review, Review, 1);
    expect(refused, Reject, accepted, Draft, Draft, 2);
    const auto denied = expect(refused, Publish, rejected, Draft, Draft, 2);
    show("rejected publish", refused, denied);

    for (bool isolate : {false, true}) {
        for (bool first : {true, false}) {
            Example<Machine> x(isolate, first);
            x.diagnostic_fails = true;
            const bool view_reached = isolate || !first;
            const auto result = expect(x, Submit, isolate ? accepted : diagnostic_error,
                                       Review, view_reached ? Review : Draft, view_reached ? 1 : 0);
            check(x.document.diagnostic_errors == (isolate ? 1 : 0), "error not counted");
            std::cout << (isolate ? "isolated, " : "baseline, ");
            show(first ? "diagnostic first" : "diagnostic last", x, result);
        }

        Example<Machine> x(isolate);
        x.view_fails = true;
        expect(x, Submit, required_error, Review, Draft, 0);
        x.view_fails = false;
        expect(x, Submit, rejected, Review, Draft, 0);
        check(x.document.notifications == 1, "rejected retry sent notifications");
        expect(x, Approve, accepted, Approved, Approved, 1);
        check(x.document.notifications == 2, "wrong continuation notification count");
        check(x.document.diagnostic_errors == 0, "required error was isolated");
    }
    std::cout << "required failure, both modes: Review/Draft/0 -> retry rejected -> Approved/Approved/1\n";

    Example<Machine> mandatory(true);
    mandatory.view_throws_diagnostic_error = true;
    expect(mandatory, Submit, diagnostic_error, Review, Draft, 0);
    check(mandatory.document.diagnostic_errors == 0, "mandatory diagnostic error was isolated");
    Example<Machine> unexpected(true);
    unexpected.unexpected_diagnostic_error = true;
    expect(unexpected, Submit, unexpected_error, Review, Draft, 0);
    check(unexpected.document.diagnostic_errors == 0, "unexpected error was isolated");
    std::cout << "wrong error type or subscriber role: errors propagated\n";
}

int main() {
    try {
        run<StateMachine>("StateMachine");
        run<TableMachine>("TableMachine");
        std::cout << "PASS: both transition implementations and Observer checks\n";
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
