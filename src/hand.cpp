#include "hand.h"
#include <sstream>
#include <algorithm>
#include <numeric>

Hand::Hand(GameState& state, BotIO& io)
    : state_(state), io_(io) {
    int n = state_.num_players();
    round_bets_.resize(n, 0);
    total_invested_.resize(n, 0);
}

bool Hand::run() {
    auto& players = state_.players();
    int n = state_.num_players();

    // reset player hand state
    for (auto& p : players) {
        if (!p.eliminated) {
            p.reset_for_hand();
        }
    }

    state_.increment_hand();
    int hand_num = state_.hand_number();
    int sb = state_.small_blind();
    int bb = state_.big_blind();
    int dealer = state_.dealer_seat();
    int sb_seat = state_.small_blind_seat();
    int bb_seat = state_.big_blind_seat();

    // log hand start
    state_.history().start_hand(hand_num, dealer, sb, bb);
    auto& record = state_.history().current_hand();
    record.add_event(EventType::HAND_START, -1, Street::PREFLOP);

    // broadcast HAND_START and CHIPS
    {
        std::ostringstream oss;
        oss << "HAND_START " << hand_num << " " << dealer
            << " " << sb_seat << " " << bb_seat
            << " " << sb << " " << bb;
        io_.broadcast(oss.str());
    }
    {
        std::ostringstream oss;
        oss << "CHIPS";
        for (int i = 0; i < n; i++) {
            oss << " " << players[i].chips;
        }
        io_.broadcast(oss.str());
    }

    // deal hole cards
    deck_ = Deck();
    deck_.shuffle(state_.rng());
    deal_hole_cards();

    // post blinds
    if (!post_blinds()) return true;

    // pre-flop swap
    current_street_ = Street::PREFLOP;
    swap_phase(Street::PREFLOP);
    if (check_single_winner()) return true;

    // pre-flop betting
    betting_round(Street::PREFLOP);
    if (check_single_winner()) return true;

    // flop
    current_street_ = Street::FLOP;
    if (!deal_community(Street::FLOP)) {
        showdown();
        return true;
    }
    swap_phase(Street::FLOP);
    if (check_single_winner()) return true;
    vote_phase(Street::FLOP);
    if (check_single_winner()) return true;
    betting_round(Street::FLOP);
    if (check_single_winner()) return true;

    // turn
    current_street_ = Street::TURN;
    if (!deal_community(Street::TURN)) {
        showdown();
        return true;
    }
    swap_phase(Street::TURN);
    if (check_single_winner()) return true;
    vote_phase(Street::TURN);
    if (check_single_winner()) return true;
    betting_round(Street::TURN);
    if (check_single_winner()) return true;

    // river
    current_street_ = Street::RIVER;
    if (!deal_community(Street::RIVER)) {
        showdown();
        return true;
    }
    swap_phase(Street::RIVER);
    if (check_single_winner()) return true;
    vote_phase(Street::RIVER);
    if (check_single_winner()) return true;
    betting_round(Street::RIVER);
    if (check_single_winner()) return true;

    // showdown
    showdown();
    return true;
}

void Hand::deal_hole_cards() {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();

    for (auto& p : players) {
        if (p.eliminated) continue;
        p.hole_cards[0] = deck_.draw_one();
        p.hole_cards[1] = deck_.draw_one();

        auto& ev = record.add_event_ref(EventType::DEAL_HOLE, p.seat, Street::PREFLOP);
        ev.cards = {p.hole_cards[0], p.hole_cards[1]};

        io_.send(p.seat, "DEAL_HOLE " + card_to_string(p.hole_cards[0]) +
                 " " + card_to_string(p.hole_cards[1]));
    }
}

bool Hand::deal_community(Street street) {
    auto& record = state_.history().current_hand();
    int num_cards = (street == Street::FLOP) ? 3 : 1;
    if (deck_.cards_remaining() < num_cards) {
        return false;
    }
    auto drawn = deck_.draw(num_cards);

    for (auto& c : drawn) {
        community_cards_.push_back(c);
    }

    auto& ev = record.add_event_ref(EventType::DEAL_COMMUNITY, -1, street);
    ev.cards = drawn;

    std::ostringstream oss;
    if (street == Street::FLOP) {
        oss << "DEAL_FLOP";
    } else if (street == Street::TURN) {
        oss << "DEAL_TURN";
    } else {
        oss << "DEAL_RIVER";
    }
    for (auto& c : drawn) {
        oss << " " << card_to_string(c);
    }
    io_.broadcast(oss.str());
    return true;
}

bool Hand::post_blinds() {
    auto& players = state_.players();
    int sb_seat = state_.small_blind_seat();
    int bb_seat = state_.big_blind_seat();
    int sb = state_.small_blind();
    int bb = state_.big_blind();

    // small blind
    int sb_post = std::min(sb, players[sb_seat].chips);
    players[sb_seat].chips -= sb_post;
    small_blind_post_ = sb_post;
    round_bets_[sb_seat] = sb_post;
    total_invested_[sb_seat] += sb_post;
    pot_ += sb_post;
    if (players[sb_seat].chips == 0) players[sb_seat].all_in = true;

    // big blind
    int bb_post = std::min(bb, players[bb_seat].chips);
    players[bb_seat].chips -= bb_post;
    big_blind_post_ = bb_post;
    round_bets_[bb_seat] = bb_post;
    total_invested_[bb_seat] += bb_post;
    pot_ += bb_post;
    if (players[bb_seat].chips == 0) players[bb_seat].all_in = true;

    current_bet_ = bb_post;
    last_raise_size_ = bb;

    return true;
}

void Hand::swap_phase(Street street) {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();
    int cost = state_.swap_cost(street);
    phase_force_folds_.clear();
    int reserve_cards = (street == Street::PREFLOP && community_cards_.size() < 3)
        ? (3 - static_cast<int>(community_cards_.size()))
        : 0;

    // track which players are still eligible for swapping
    std::vector<bool> eligible(state_.num_players(), false);
    for (auto& p : players) {
        if (p.can_act() && p.chips >= cost) {
            eligible[p.seat] = true;
        }
    }

    while (true) {
        // check if anyone is eligible
        bool any_eligible = false;
        int eligible_count = 0;
        for (int i = 0; i < state_.num_players(); i++) {
            if (eligible[i]) {
                any_eligible = true;
                eligible_count++;
            }
        }
        if (!any_eligible) break;
        if (deck_.cards_remaining() - reserve_cards < eligible_count) break;

        // send SWAP_PROMPT to all eligible players
        std::vector<int> prompted_seats;
        for (int i = 0; i < state_.num_players(); i++) {
            if (!eligible[i]) continue;
            prompted_seats.push_back(i);
            std::ostringstream oss;
            oss << "SWAP_PROMPT " << cost << " " << players[i].chips;
            io_.send(i, oss.str());
        }

        // collect responses
        bool anyone_swapped = false;
        for (int i : prompted_seats) {
            std::string response = io_.recv(i);
            if (response.empty()) {
                force_fold(i);
                eligible[i] = false;
                continue;
            }

            std::istringstream iss(response);
            std::string cmd;
            iss >> cmd;

            if (cmd == "STAY") {
                record.add_event_ref(EventType::SWAP_STAY, i, street);
                eligible[i] = false;
            } else if (cmd == "SWAP") {
                int idx;
                if (!(iss >> idx) || (idx != 0 && idx != 1)) {
                    force_fold(i);
                    eligible[i] = false;
                    continue;
                }

                // pay the cost
                players[i].chips -= cost;
                pot_ += cost;
                total_invested_[i] += cost;
                if (players[i].chips == 0) players[i].all_in = true;

                // discard old card (don't return to deck), draw new one
                Card old_card = players[i].hole_cards[idx];
                Card new_card = deck_.draw_one();
                players[i].hole_cards[idx] = new_card;

                auto& ev = record.add_event_ref(EventType::SWAP, i, street);
                ev.cards = {old_card, new_card};
                ev.amount = cost;

                io_.send(i, "SWAP_RESULT " + card_to_string(new_card));
                anyone_swapped = true;

                // check if player can still afford another swap
                if (players[i].chips < cost) {
                    eligible[i] = false;
                }
            } else {
                force_fold(i);
                eligible[i] = false;
            }
        }

        if (count_active() <= 1) break;
        if (!anyone_swapped) break;
    }

    io_.broadcast("SWAP_DONE");
}

void Hand::vote_phase(Street street) {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();
    phase_force_folds_.clear();

    if (count_active() <= 1) return;

    int yes_total = 0, no_total = 0;

    // send VOTE_PROMPT to all active players
    std::vector<int> prompted_seats;
    for (auto& p : players) {
        if (!p.is_active()) continue;
        prompted_seats.push_back(p.seat);

        std::ostringstream oss;
        oss << "VOTE_PROMPT " << p.chips;
        io_.send(p.seat, oss.str());
    }

    // collect votes
    for (int seat : prompted_seats) {
        auto& p = players[seat];

        std::string response = io_.recv(p.seat);
        if (response.empty()) {
            force_fold(p.seat);
            continue;
        }

        std::istringstream iss(response);
        std::string cmd, direction;
        int amount;
        iss >> cmd >> direction >> amount;

        if (cmd != "VOTE" || (direction != "YES" && direction != "NO")) {
            force_fold(p.seat);
            continue;
        }

        if (amount < 0 || amount > p.chips) {
            force_fold(p.seat);
            continue;
        }

        p.chips -= amount;
        pot_ += amount;
        total_invested_[p.seat] += amount;
        if (p.chips == 0) p.all_in = true;

        if (direction == "YES") {
            yes_total += amount;
        } else {
            no_total += amount;
        }

        auto& ev = record.add_event_ref(EventType::VOTE, p.seat, street);
        ev.action = direction;
        ev.amount = amount;
    }

    bool kept = (yes_total >= no_total);
    int redraw_cards_needed = (street == Street::FLOP) ? 3 : 1;
    if (!kept && deck_.cards_remaining() < redraw_cards_needed) {
        kept = true;
    }

    // log vote result
    auto& vr = record.add_event_ref(EventType::VOTE_RESULT, -1, street);
    vr.amount = yes_total;
    vr.amount2 = no_total;
    vr.action = kept ? "KEPT" : "REDRAWN";

    {
        std::ostringstream oss;
        oss << "VOTE_RESULT " << yes_total << " " << no_total
            << " " << (kept ? "KEPT" : "REDRAWN");
        io_.broadcast(oss.str());
    }

    // if vote says redraw, replace community cards for this street
    if (!kept) {
        if (street == Street::FLOP) {
            // remove last 3 community cards, redraw 3
            for (int i = 0; i < 3; i++) community_cards_.pop_back();
        } else {
            // remove last 1, redraw 1
            community_cards_.pop_back();
        }

        auto drawn = deck_.draw(redraw_cards_needed);
        for (auto& c : drawn) {
            community_cards_.push_back(c);
        }

        auto& ev = record.add_event_ref(EventType::COMMUNITY_REDRAW, -1, street);
        ev.cards = drawn;

        std::ostringstream oss;
        if (street == Street::FLOP) oss << "REDRAW_FLOP";
        else if (street == Street::TURN) oss << "REDRAW_TURN";
        else oss << "REDRAW_RIVER";
        for (auto& c : drawn) {
            oss << " " << card_to_string(c);
        }
        io_.broadcast(oss.str());
    }
}

void Hand::betting_round(Street street) {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();
    int n = state_.num_players();
    phase_force_folds_.clear();

    // nothing to do if 0 or 1 active players, or nobody can act
    if (count_active() <= 1 || count_can_act() == 0) return;

    // reset round bets
    std::fill(round_bets_.begin(), round_bets_.end(), 0);
    current_bet_ = 0;
    last_raise_size_ = state_.big_blind();

    if (street == Street::PREFLOP) {
        // Pre-flop round bets should only reflect the blinds, not swap costs.
        int sb_s = state_.small_blind_seat();
        int bb_s = state_.big_blind_seat();
        round_bets_[sb_s] = small_blind_post_;
        round_bets_[bb_s] = big_blind_post_;
        current_bet_ = big_blind_post_;
        last_raise_size_ = state_.big_blind();
    }

    // determine first actor
    int first = -1;
    {
        int start = (street == Street::PREFLOP)
            ? state_.next_active_seat(state_.big_blind_seat())
            : state_.next_active_seat(state_.dealer_seat());
        int seat = start;
        for (int i = 0; i < n; i++) {
            if (players[seat].can_act()) { first = seat; break; }
            seat = state_.next_active_seat(seat);
        }
    }
    if (first == -1) return;

    // track who has had a chance to act
    std::vector<bool> has_acted(n, false);
    int actor = first;

    while (true) {
        // skip folded, eliminated, all-in
        if (!players[actor].can_act()) {
            actor = state_.next_active_seat(actor);
            if (count_can_act() == 0) break;
            continue;
        }

        // check if round is over: everyone who can act has acted and bets are equal
        if (has_acted[actor]) {
            bool all_even = true;
            for (int i = 0; i < n; i++) {
                if (players[i].can_act() && round_bets_[i] != current_bet_) {
                    all_even = false;
                    break;
                }
            }
            if (all_even) break;
        }

        // send ACTION_PROMPT
        int min_raise = current_bet_ + last_raise_size_;
        {
            std::ostringstream oss;
            oss << "ACTION_PROMPT " << players[actor].chips
                << " " << current_bet_
                << " " << round_bets_[actor]
                << " " << min_raise
                << " " << pot_;
            io_.send(actor, oss.str());
        }

        std::string response = io_.recv(actor);
        if (response.empty()) {
            force_fold(actor);
            auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
            ev.action = "FOLD";
            io_.broadcast("ACTION " + std::to_string(actor) + " FOLD");
            has_acted[actor] = true;
            if (check_single_winner()) return;
            actor = state_.next_active_seat(actor);
            continue;
        }

        std::istringstream iss(response);
        std::string cmd;
        iss >> cmd;

        bool valid = true;
        std::string broadcast_msg;

        if (cmd == "FOLD") {
            players[actor].folded = true;
            auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
            ev.action = "FOLD";
            broadcast_msg = "ACTION " + std::to_string(actor) + " FOLD";
        } else if (cmd == "CHECK") {
            if (current_bet_ != round_bets_[actor]) {
                valid = false;
            } else {
                auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
                ev.action = "CHECK";
                broadcast_msg = "ACTION " + std::to_string(actor) + " CHECK";
            }
        } else if (cmd == "CALL") {
            int to_call = current_bet_ - round_bets_[actor];
            if (to_call <= 0) {
                valid = false;
            } else {
                int actual = std::min(to_call, players[actor].chips);
                players[actor].chips -= actual;
                round_bets_[actor] += actual;
                total_invested_[actor] += actual;
                pot_ += actual;
                if (players[actor].chips == 0) players[actor].all_in = true;

                auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
                ev.action = "CALL";
                ev.amount = round_bets_[actor];
                broadcast_msg = "ACTION " + std::to_string(actor) + " CALL " +
                                std::to_string(round_bets_[actor]);
            }
        } else if (cmd == "RAISE") {
            int amount;
            if (!(iss >> amount)) { valid = false; }
            else {
                if (amount < min_raise || amount > players[actor].chips + round_bets_[actor]) {
                    // allow all-in for less than min raise
                    if (amount == players[actor].chips + round_bets_[actor]) {
                        // all-in raise, ok
                    } else {
                        valid = false;
                    }
                }
                if (valid) {
                    int raise_increment = amount - current_bet_;
                    int cost = amount - round_bets_[actor];
                    players[actor].chips -= cost;
                    pot_ += cost;
                    total_invested_[actor] += cost;
                    round_bets_[actor] = amount;
                    if (raise_increment > last_raise_size_) last_raise_size_ = raise_increment;
                    current_bet_ = amount;
                    if (players[actor].chips == 0) players[actor].all_in = true;

                    // reset acted flags for everyone else
                    std::fill(has_acted.begin(), has_acted.end(), false);

                    auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
                    ev.action = "RAISE";
                    ev.amount = amount;
                    broadcast_msg = "ACTION " + std::to_string(actor) + " RAISE " +
                                    std::to_string(amount);
                }
            }
        } else if (cmd == "ALLIN") {
            int allin_amount = players[actor].chips + round_bets_[actor];
            int cost = players[actor].chips;
            if (allin_amount > current_bet_) {
                int raise_increment = allin_amount - current_bet_;
                if (raise_increment >= last_raise_size_) {
                    last_raise_size_ = raise_increment;
                    // reset acted flags for everyone else
                    std::fill(has_acted.begin(), has_acted.end(), false);
                }
                current_bet_ = allin_amount;
            }
            players[actor].chips = 0;
            pot_ += cost;
            total_invested_[actor] += cost;
            round_bets_[actor] = allin_amount;
            players[actor].all_in = true;

            auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
            ev.action = "ALLIN";
            ev.amount = allin_amount;
            broadcast_msg = "ACTION " + std::to_string(actor) + " ALLIN " +
                            std::to_string(allin_amount);
        } else {
            valid = false;
        }

        if (!valid) {
            force_fold(actor);
            auto& ev = record.add_event_ref(EventType::BET_ACTION, actor, street);
            ev.action = "FOLD";
            broadcast_msg = "ACTION " + std::to_string(actor) + " FOLD";
        }

        io_.broadcast(broadcast_msg);
        has_acted[actor] = true;

        if (check_single_winner()) return;
        actor = state_.next_active_seat(actor);
    }
}

void Hand::showdown() {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();

    // reveal hands
    for (auto& p : players) {
        if (!p.is_active()) continue;
        io_.broadcast("SHOWDOWN " + std::to_string(p.seat) + " " +
                       card_to_string(p.hole_cards[0]) + " " +
                       card_to_string(p.hole_cards[1]));

        auto& ev = record.add_event_ref(EventType::SHOWDOWN, p.seat, Street::RIVER);
        ev.cards = {p.hole_cards[0], p.hole_cards[1]};
    }

    resolve_pots();
}

void Hand::resolve_pots() {
    auto& players = state_.players();
    auto& record = state_.history().current_hand();
    int n = state_.num_players();

    // collect active players and their investments
    struct Contestant {
        int seat;
        int invested;
        HandScore score;
    };

    std::vector<Contestant> contestants;
    for (auto& p : players) {
        if (!p.is_active()) continue;
        std::vector<Card> all_cards(community_cards_.begin(), community_cards_.end());
        all_cards.push_back(p.hole_cards[0]);
        all_cards.push_back(p.hole_cards[1]);
        contestants.push_back({p.seat, total_invested_[p.seat], evaluate_hand(all_cards)});
    }

    if (contestants.empty()) return;

    // sort by investment amount for side-pot calculation
    auto by_invested = contestants;
    std::sort(by_invested.begin(), by_invested.end(),
              [](const auto& a, const auto& b) { return a.invested < b.invested; });

    int processed = 0;
    for (size_t i = 0; i < by_invested.size(); i++) {
        int cap = by_invested[i].invested;
        if (cap <= processed) continue;

        int this_level = cap - processed;
        int pot_slice = 0;

        // everyone who invested at least this much contributes
        for (int j = 0; j < n; j++) {
            int contrib = std::min(total_invested_[j] - processed, this_level);
            if (contrib > 0) pot_slice += contrib;
        }

        // find best hand among contestants still eligible (invested >= cap)
        HandScore best_score;
        bool first = true;
        for (auto& c : contestants) {
            if (c.invested < cap) continue;
            if (first || c.score > best_score) {
                best_score = c.score;
                first = false;
            }
        }

        // find all winners at this level
        std::vector<int> winners;
        for (auto& c : contestants) {
            if (c.invested < cap) continue;
            if (c.score == best_score) {
                winners.push_back(c.seat);
            }
        }

        if (pot_slice == 0) { processed = cap; continue; }

        // order winners by proximity to dealer's left for remainder chips
        int dealer = state_.dealer_seat();
        std::sort(winners.begin(), winners.end(), [&](int a, int b) {
            int nm = state_.num_players();
            int dist_a = ((a - dealer - 1) % nm + nm) % nm;
            int dist_b = ((b - dealer - 1) % nm + nm) % nm;
            return dist_a < dist_b;
        });

        // split pot_slice among winners
        int share = pot_slice / static_cast<int>(winners.size());
        int remainder = pot_slice % static_cast<int>(winners.size());

        for (size_t w = 0; w < winners.size(); w++) {
            int award = share + (static_cast<int>(w) < remainder ? 1 : 0);
            players[winners[w]].chips += award;

            auto& ev = record.add_event_ref(EventType::WINNER, winners[w], Street::RIVER);
            ev.amount = award;
            ev.hand_rank = best_score.rank;

            io_.broadcast("WINNER " + std::to_string(winners[w]) + " " +
                          std::to_string(award) + " " +
                          hand_rank_to_string(best_score.rank));
        }

        processed = cap;
    }
}

int Hand::count_active() const {
    int count = 0;
    for (const auto& p : state_.players()) {
        if (p.is_active()) count++;
    }
    return count;
}

int Hand::count_can_act() const {
    int count = 0;
    for (const auto& p : state_.players()) {
        if (p.can_act()) count++;
    }
    return count;
}

bool Hand::check_single_winner() {
    int active = count_active();
    if (active <= 1) {
        if (pot_ > 0) {
            auto& players = state_.players();

            if (active == 1) {
                // one player left — they win the pot
                for (auto& p : players) {
                    if (p.is_active()) {
                        p.chips += pot_;
                        auto& record = state_.history().current_hand();
                        auto& ev = record.add_event_ref(EventType::WINNER, p.seat, current_street_);
                        ev.amount = pot_;
                        ev.action = "FOLD_WIN";
                        io_.broadcast("WINNER " + std::to_string(p.seat) + " " +
                                      std::to_string(pot_) + " FOLD_WIN");
                        break;
                    }
                }
            } else if (!phase_force_folds_.empty()) {
                // everyone folded — split pot among players who IO-errored in the last phase
                // order by proximity to dealer's left for remainder chips
                int dealer = state_.dealer_seat();
                std::vector<int> ordered = phase_force_folds_;
                std::sort(ordered.begin(), ordered.end(), [&](int a, int b) {
                    int n = state_.num_players();
                    int dist_a = ((a - dealer - 1) % n + n) % n;
                    int dist_b = ((b - dealer - 1) % n + n) % n;
                    return dist_a < dist_b;
                });

                int n_winners = static_cast<int>(ordered.size());
                int share = pot_ / n_winners;
                int remainder = pot_ % n_winners;

                for (int i = 0; i < n_winners; i++) {
                    int seat = ordered[i];
                    int award = share + (i < remainder ? 1 : 0);
                    players[seat].chips += award;

                    auto& record = state_.history().current_hand();
                    auto& ev = record.add_event_ref(EventType::WINNER, seat, current_street_);
                    ev.amount = award;
                    ev.action = "ERROR_SPLIT";
                    io_.broadcast("WINNER " + std::to_string(seat) + " " +
                                  std::to_string(award) + " ERROR_SPLIT");
                }
            }
            pot_ = 0;
        }
        return true;
    }
    return false;
}

void Hand::force_fold(int seat) {
    state_.players()[seat].folded = true;
    phase_force_folds_.push_back(seat);
}
