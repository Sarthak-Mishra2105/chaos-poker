#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <random>
#include <chrono>
#include <cmath>
#include <cstdint>

enum class Suit : uint8_t { SPADES, HEARTS, DIAMONDS, CLUBS };
enum class Rank : uint8_t { 
    TWO=2, THREE, FOUR, FIVE, SIX, SEVEN, EIGHT, NINE, TEN, JACK, QUEEN, KING, ACE 
};
enum class HandRank : uint8_t {
    HIGH_CARD, ONE_PAIR, TWO_PAIR, THREE_OF_A_KIND, STRAIGHT, FLUSH, FULL_HOUSE, FOUR_OF_A_KIND, STRAIGHT_FLUSH, ROYAL_FLUSH
};

struct Card {
    Rank rank;
    Suit suit;
    bool operator==(const Card& o) const {
        return rank == o.rank && suit == o.suit;
    }
};

struct HandScore {
    HandRank rank;
    std::array<int, 5> kickers;
    bool operator>(const HandScore& o) const {
        if (rank != o.rank) return rank > o.rank;
        return kickers > o.kickers;
    }
    bool operator==(const HandScore& o) const {
        return rank == o.rank && kickers == o.kickers;
    }
};

struct OpponentStats {
    int hands_seen = 0;
    int vpip_count = 0;
    int pfr_count = 0;
    int fold_count = 0;
    int action_count = 0;
    int total_bets_raises = 0;
    int total_calls_checks = 0;
    bool eliminated = false;

    float fold_rate() const {
        return action_count > 0 ? (float)fold_count / action_count : 0.3f;
    }
    float vpip() const {
        return hands_seen > 0 ? (float)vpip_count / hands_seen : 0.5f;
    }
    float aggression() const {
        int d = total_calls_checks;
        if (d > 0) return (float)total_bets_raises / d;
        return total_bets_raises > 3 ? 10.0f : 1.0f;
    }
};

struct GameState {
    int num_players = 0, my_seat = -1, starting_chips = 0;
    int swap_mult[4] = {};
    int hand_num = 0, dealer = 0;
    int sb_seat = 0, bb_seat = 0, sb_amount = 0, bb_amount = 0;

    std::vector<int> chips;
    std::array<Card, 2> hole;
    std::vector<Card> community;
    std::vector<Card> discarded;
    std::vector<OpponentStats> opp_stats;
    int street = 0;
    std::vector<bool> folded;
    std::vector<bool> all_in_flag;
    int pot_estimate = 0;
    int swaps_this_phase = 0;

    int non_folded_opponents() const {
        int cnt = 0;
        for (int i = 0; i < num_players; i++) {
            if (i == my_seat) continue;
            if (!folded[i] && !opp_stats[i].eliminated) cnt++;
        }
        return cnt;
    }
    float avg_opp_fold_rate() const {
        float sum = 0; int cnt = 0;
        for (int i = 0; i < num_players; i++) {
            if (i == my_seat || folded[i] || opp_stats[i].eliminated) continue;
            sum += opp_stats[i].fold_rate();
            cnt++;
        }
        return cnt > 0 ? sum / cnt : 1.0f;
    }
    float avg_opp_aggression() const {
        float sum = 0; int cnt = 0;
        for (int i = 0; i < num_players; i++) {
            if (i == my_seat || folded[i] || opp_stats[i].eliminated) continue;
            sum += opp_stats[i].aggression();
            cnt++;
        }
        return cnt > 0 ? sum / cnt : 1.0f;
    }
    bool is_late_position() const {
        int rel = (my_seat - dealer + num_players) % num_players;
        if (num_players <= 3) return rel == 0;
        return rel == 0 || rel == num_players - 1;
    }
    bool is_chip_leader() const {
        int my = chips.empty() ? 0 : chips[my_seat];
        for (int i = 0; i < num_players; i++) {
            if (i == my_seat || opp_stats[i].eliminated) continue;
            if (chips[i] > my) return false;
        }
        return true;
    }
};

Rank char_to_rank(char c) {
    switch(c) {
        case '2': return Rank::TWO;
        case '3': return Rank::THREE;
        case '4': return Rank::FOUR;
        case '5': return Rank::FIVE;
        case '6': return Rank::SIX;
        case '7': return Rank::SEVEN;
        case '8': return Rank::EIGHT;
        case '9': return Rank::NINE;
        case 'T': return Rank::TEN;
        case 'J': return Rank::JACK;
        case 'Q': return Rank::QUEEN;
        case 'K': return Rank::KING;
        case 'A': return Rank::ACE;
        default: return Rank::TWO;
    }
}

Suit char_to_suit(char c) {
    switch(c) {
        case 's': return Suit::SPADES;
        case 'h': return Suit::HEARTS;
        case 'd': return Suit::DIAMONDS;
        case 'c': return Suit::CLUBS;
        default: return Suit::SPADES;
    }
}

Card parse_card(const std::string& s) {
    return {char_to_rank(s[0]), char_to_suit(s[1])};
}

HandScore evaluate_five(const std::array<Card, 5>& cards) {
    std::array<int, 5> vals, suits;
    for (int i = 0; i < 5; i++){
        vals[i] = static_cast<int>(cards[i].rank);
        suits[i] = static_cast<int>(cards[i].suit);
    }

    // Sort vals in descending order
    std::sort(vals.begin(), vals.end(), std::greater<int>());

    // Check for flush
    bool is_flush = (suits[0] == suits[1] && 
        suits[1] == suits[2] && 
        suits[2] == suits[3] && 
        suits[3] == suits[4]
    );
    
    // Check for straight
    bool is_straight = false;
    int straight_high = 0;
    if (vals[0] - vals[4] == 4 &&
        vals[0] != vals[1] &&
        vals[1] != vals[2] &&
        vals[2] != vals[3] &&
        vals[3] != vals[4]) {
        is_straight = true;
        straight_high = vals[0];
    }

    // Wheel straight (A-2-3-4-5)
    if (vals[0] == 14 && vals[1] == 5 &&
        vals[2] == 4 && vals[3] == 3 && vals[4] == 2) {
        is_straight = true;
        straight_high = 5;
    }

    // Count frequency of each rank
    int count[15] = {0};
    for (int v : vals) count[v]++;

    int four_val = 0, three_val = 0, pairs[2] = {}, pair_count = 0;
    for (int v = 14; v >= 2; v--) {
        if (count[v] == 4) four_val = v;
        else if (count[v] == 3) three_val = v;
        else if (count[v] == 2 && pair_count < 2) pairs[pair_count++] = v;
    }

    // Determine hand rank
    HandScore hs;
    hs.kickers = {};

    if (is_straight && is_flush) {
        hs.rank = (straight_high == 14) ? HandRank::ROYAL_FLUSH : HandRank::STRAIGHT_FLUSH;
        hs.kickers[0] = straight_high;
    }
    else if (four_val) {
        hs.rank = HandRank::FOUR_OF_A_KIND;
        hs.kickers[0] = four_val;
        for (int v : vals) {
            if (v != four_val) {
                hs.kickers[1] = v;
                break;
            }
        }
    }

    else if (three_val && pair_count >= 1) {
        hs.rank = HandRank::FULL_HOUSE;
        hs.kickers[0] = three_val;
        hs.kickers[1] = pairs[0];
    }

    else if (is_flush) {
        hs.rank = HandRank::FLUSH;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    }
    
    else if (is_straight) {
        hs.rank = HandRank::STRAIGHT;
        hs.kickers[0] = straight_high;
    }

    else if (three_val) {
        hs.rank = HandRank::THREE_OF_A_KIND;
        hs.kickers[0] = three_val;
        int ki = 1;
        for (int v : vals) {
            if (v != three_val) {
                hs.kickers[ki++] = v;
            }
        }
    }

    else if (pair_count == 2) {
        hs.rank = HandRank::TWO_PAIR;
        hs.kickers[0] = pairs[0];
        hs.kickers[1] = pairs[1];
        for (int v : vals) {
            if (v != pairs[0] && v != pairs[1]) {
                hs.kickers[2] = v;
                break;
            }
        }
    }

    else if (pair_count == 1) {
        hs.rank = HandRank::ONE_PAIR;
        hs.kickers[0] = pairs[0];
        int ki = 1;
        for (int v : vals) {
            if (v != pairs[0]) {
                hs.kickers[ki++] = v;
            }
        }
    }

    else {
        hs.rank = HandRank::HIGH_CARD;
        for (int i = 0; i < 5; i++) hs.kickers[i] = vals[i];
    }

    return hs;
}

HandScore evaluate_hand(const std::vector<Card>& cards) {
    int n = (int)cards.size();
    HandScore best;
    best.rank = HandRank::HIGH_CARD;
    best.kickers = {};
    bool first = true;

    for (int a = 0; a < n-4; a++) {
        for (int b = a+1; b < n-3; b++) {
            for (int c = b+1; c < n-2; c++) {
                for (int d = c+1; d < n-1; d++) {
                    for (int e = d+1; e < n; e++) {
                        std::array<Card, 5> hand = {cards[a], cards[b], cards[c], cards[d], cards[e]};
                        HandScore hs = evaluate_five(hand);
                        if (first || hs > best) {
                            best = hs;
                            first = false;
                        }
                    }
                }
            }
        }
    }
    return best;
}

const std::vector<Card>& full_deck() {
    static std::vector<Card> d = []() {
        std::vector<Card> v;
        v.reserve(52);
        for (int s = 0; s < 4; s++) {
            for (int r = 2; r <= 14; r++) {
                v.push_back({static_cast<Rank>(r), static_cast<Suit>(s)});
            }
        }
        return v;
    }();
    return d;
}

bool card_in(const Card& c, const std::vector<Card>& v) {
    for (const Card& x : v) {
        if (c == x) return true;
    }
    return false;
}

std::vector<Card> build_unseen(const std::array<Card, 2>& hole,
    const std::vector<Card>& community,
    const std::vector<Card>& discarded) {
    std::vector<Card> unseen;
    for (const Card& c : full_deck()) {
        if (c == hole[0] || c == hole[1]) continue;
        if (card_in(c, community)) continue;
        if (card_in(c, discarded)) continue;
        unseen.push_back(c);
    }
    return unseen;
}

static std::mt19937 rng(
    std::chrono::steady_clock::now().time_since_epoch().count());

float estimate_equity(const std::array<Card, 2>& hole,
    const std::vector<Card>& community,
    const std::vector<Card>& discarded,
    int num_opponents,
    int max_sims = 3000,
    int time_budget_us = 5500) {
    
    if (num_opponents <= 0 ) return 1.0f;

    auto unseen = build_unseen(hole, community, discarded);
    int n = (int)unseen.size();
    int cards_needed = (5 - (int)community.size()) + 2 * num_opponents;
    if (n < cards_needed) return 1.0f;

    auto start = std::chrono::steady_clock::now();
    float score_sum = 0.0f;
    int sims = 0;

    for (sims = 0; sims < max_sims; sims++) {
        // Check time budget every 64 simulations, with bitwise operations
        if (sims > 0 && (sims & 63) == 0) {
            auto elapsed = std::chrono::duration_cast<
            std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (elapsed >= time_budget_us) break;
        }
        
        // Partial Fisher-Yates shuffle to get random cards
        for (int i = 0; i < cards_needed; i++) {
            int j = i + (rng() % (n - i));
            std::swap(unseen[i], unseen[j]);
        }

        int idx = 0;
        // Complete community
        std::vector<Card> sim_comm = community;
        while ((int)sim_comm.size() < 5)
            sim_comm.push_back(unseen[idx++]);
            
        // check own hand
        std::vector<Card> my_cards(sim_comm.begin(), sim_comm.end());
        my_cards.push_back(hole[0]);
        my_cards.push_back(hole[1]);
        HandScore mine = evaluate_hand(my_cards);

        // check opponents, count ties
        bool i_win = true;
        int tie_count = 0;
        for (int o = 0; o < num_opponents; o++) {
            std::vector<Card> opp(sim_comm.begin(), sim_comm.end());
            opp.push_back(unseen[idx++]);
            opp.push_back(unseen[idx++]);
            HandScore s = evaluate_hand(opp);
            if (s > mine) {
                i_win = false;
                break;
            }
            if (s == mine) tie_count++;
        }
        if (i_win) {
            // 1.0 for win, 1/(tie_count+1) for tie
            score_sum += 1.0f / (tie_count + 1);
        }
    }
    if (sims == 0) return 0.5f;
    return score_sum / sims;
}

float estimate_swap_equity(GameState& gs, int swap_idx, int sample_count = 30, 
int mc_sims = 100, int mc_budget_us = 1500) {
    auto unseen = build_unseen(gs.hole, gs.community, gs.discarded);
    int n = (int)unseen.size();
    if (n == 0) return 0.0f;

    float total = 0.0f;
    int num_opp = gs.non_folded_opponents();
    auto start = std::chrono::steady_clock::now();
    int actual = 0;

    for (int s = 0; s < sample_count && n > 0; s++) {
        // Check time every 10 samples
        if (s > 0 && (s % 10) == 0) {
            auto us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - start
            ).count();
            if (us >= mc_budget_us) break;
        }

        int ri = rng() % n;
        Card replacement = unseen[ri];
        auto test_hole = gs.hole;
        test_hole[swap_idx] = replacement;
        auto test_disc = gs.discarded;
        test_disc.push_back(gs.hole[swap_idx]);
        total += estimate_equity(test_hole, gs.community, test_disc, num_opp, mc_sims, mc_budget_us / sample_count);
        actual++;
    }
    return actual > 0 ? total / actual : 0.0f;
}

float chen_score(const std::array<Card, 2>& hole) {
    int r1 = static_cast<int>(hole[0].rank);
    int r2 = static_cast<int>(hole[1].rank);
    if (r1 < r2) std::swap(r1, r2);
    bool suited = (hole[0].suit == hole[1].suit);
    bool paired = (r1 == r2);

    float score;
    if (r1 == 14) score = 10;
    else if (r1 == 13) score = 8;
    else if (r1 == 12) score = 7;
    else if (r1 == 11) score = 6;
    else score = r1 / 2.0f;

    if (paired) {
        score *=2;
        if (score < 5) score = 5;
    }
    if (suited) score += 2;
    if (!paired) {
        int gap = r1 - r2;
        if (gap == 1) score += 1;
        else if (gap == 2) score -= 1;
        else if (gap == 3) score -= 2;
        else if (gap == 4) score -= 4;
        else score -= 5;
    }
    return score;
}

std::string decide_swap(GameState& gs, int cost, int my_chips) {
    if (cost > my_chips / 4) return "STAY";
    if (gs.swaps_this_phase > 0) return "STAY";

    int r0 = static_cast<int>(gs.hole[0].rank);
    int r1 = static_cast<int>(gs.hole[1].rank);
    if (r0 == r1 && r0 >= 7) return "STAY";

    // use monte carlo for post-flop
    if (!gs.community.empty()) {
        std::vector<Card> all(gs.community.begin(), gs.community.end());
        all.push_back(gs.hole[0]);
        all.push_back(gs.hole[1]);
        HandScore hs = evaluate_hand(all);
        if (hs.rank >= HandRank::ONE_PAIR) return "STAY";

        // check flush draws
        int sm0 = 0, sm1 = 0;
        for (auto& c: gs.community) {
            if (c.suit == gs.hole[0].suit) sm0++;
            if (c.suit == gs.hole[1].suit) sm1++;
        }
        if (sm0 >= 3 || sm1 >= 3) return "STAY";

        if (r0 < 10 && r1 < 10 && cost <= my_chips / 6) {
            int idx = (r0 < r1) ? 0 : 1;
            float eq_stay = estimate_equity(gs.hole, gs.community, gs.discarded, 
                gs.non_folded_opponents(), 200, 1500);
                float eq_swap = estimate_swap_equity(gs, idx);
                if (eq_swap > eq_stay + 0.03f) return "SWAP " + std::to_string(idx);
        }
        return "STAY";
    }

    // for pre-flop, use chen score
    float cs = chen_score(gs.hole);
    bool suited = (gs.hole[0].suit == gs.hole[1].suit);
    if (cs >= 7) return "STAY";
    if (cost > my_chips / 8 && cs >= 4) return "STAY";
    if (suited && cs >= 3) return "STAY";

    int swap_idx = (r0 < r1) ? 0 : 1;
    return "SWAP " + std::to_string(swap_idx);
}

std::string decide_vote(GameState& gs, int my_chips) {
    if (gs.community.empty()) return "VOTE YES 0";
    int num_opp = gs.non_folded_opponents();

    // check equity with current board
    float eq_keep = estimate_equity(gs.hole, gs.community, gs.discarded,
    num_opp, 400, 1500);

    // check equity if board was redrawn
    std::vector<Card> base_comm;
    std::vector<Card> disc_if_redrawn = gs.discarded;

    if (gs.street == 1) {
        // flop redraw, all 3 cards discarded
        for (auto& c : gs.community) disc_if_redrawn.push_back(c);
    }
    else if (gs.street == 2) {
        // turn redraw, only turn card discarded
        base_comm.assign(gs.community.begin(), gs.community.begin() + 3);
        if (gs.community.size() > 3) disc_if_redrawn.push_back(gs.community[3]);
    }
    else {
        // river redraw, flop + turn stay
        base_comm.assign(gs.community.begin(), gs.community.begin() + 4);
        if (gs.community.size() > 4) disc_if_redrawn.push_back(gs.community[4]);
    }
    
    float eq_redraw = estimate_equity(gs.hole, base_comm, disc_if_redrawn,
        num_opp, 400, 1500);
    float delta = eq_keep - eq_redraw;
    int pot_est = std::max((int)(gs.pot_estimate * 2.0f), gs.bb_amount * 6);
    float swing = std::abs(delta) * pot_est * 0.6f;
    // scale max wager by conviction: strong delta gets up to chips/4
    int max_wager = (std::abs(delta) > 0.15f) ? my_chips / 4
        : (std::abs(delta) > 0.08f) ? my_chips / 6 : my_chips / 8;
    int wager = (std::abs(delta) > 0.04f) ? std::min((int)swing, max_wager) : 0;

    if (delta >= 0) return "VOTE YES " + std::to_string(wager);
    else return "VOTE NO " + std::to_string(wager);
}

std::string decide_action(GameState& gs, int chips, int current_bet,
    int my_bet, int min_raise, int pot) {
    int to_call = current_bet - my_bet;
    float eff_bb = (gs.bb_amount > 0) ? (float)chips / gs.bb_amount : 100.0f;

    // estimate equity with monte carlo
    float equity = estimate_equity(gs.hole, gs.community, gs.discarded, 
        gs.non_folded_opponents(), 1500, 5500);

    int num_opp = gs.non_folded_opponents();
    float opp_fold = gs.avg_opp_fold_rate();
    float opp_agg = gs.avg_opp_aggression();

    // range discount: reduce when opponents are hyper-aggressive (wide range)
    float agg_factor = (opp_agg > 2.5f) ? std::max(0.0f, 1.0f - (opp_agg - 2.5f) * 0.2f) : 1.0f;
    float per_opp_disc = (gs.street == 1) ? 0.01f 
    : (gs.street == 2) ? 0.015f 
    : (gs.street == 3) ? 0.02f : 0.0f;
    equity -= per_opp_disc * num_opp * agg_factor;
    bool late_pos = gs.is_late_position();

    // risk factors when chip leader (reduced vs hyper-aggressive opponents)
    float risk_adjust = gs.is_chip_leader() ? 0.03f * agg_factor : 0.0f;

    float pot_odds = (pot + to_call > 0) ? (float)to_call / (pot + to_call) : 0.0f;
    float spr = (pot > 0) ? (float)chips / pot : 20.0f;

    // helper to determine safe raise amount
    auto safe_raise = [&](float frac) {
        int r = std::max(min_raise, (int)(pot * frac));
        return std::min(r, chips + my_bet);
    };

    // short stack (< 10 BB)
    if (eff_bb < 10) {
        if (to_call == 0) {
            return (equity > 0.45f + risk_adjust) ? "ALLIN" : "CHECK";
        }
        if (equity > 0.38f + risk_adjust) return "ALLIN";
        if (equity > pot_odds) return "CALL";
        return "FOLD";
    }

    // medium stack (10 to 30 BB)
    if (eff_bb < 30) {
        if (to_call == 0) {
            if (equity > 0.68f + risk_adjust) {
                int r = safe_raise(0.55f);
                if (r >= chips + my_bet) return "ALLIN";
                return "RAISE " + std::to_string(r);
            }
            if (equity > 0.50f && late_pos) return "RAISE " + std::to_string(safe_raise(0.35f));
            return "CHECK";
        }
        if (equity > 0.70f + risk_adjust) {
            int r = safe_raise(0.60f);
            if (r >= chips + my_bet) return "ALLIN";
            return "RAISE " + std::to_string(r);
        }
        if (equity > pot_odds + 0.02f) {
            if (to_call >= chips) return "ALLIN";
            return "CALL";
        }
        return "FOLD";
    }

    // deep stack (> 30 BB)
    if (to_call == 0) {
        if (equity > 0.72f + risk_adjust) {
            float sz = (opp_agg > 1.5f) ? 0.70f : 0.55f;
            return "RAISE " + std::to_string(safe_raise(sz));
        }
        if (equity > 0.55f && spr > 3.0f) {
            float sz = late_pos ? 0.40f : 0.35f;
            return "RAISE " + std::to_string(safe_raise(sz));
        }
        // semibluff with fold equity
        if (equity > 0.30f && opp_fold > 0.35f && late_pos) {
            float all_fold = std::pow(opp_fold, num_opp);
            float bet_frac = 0.35f;
            float bluff_ev = all_fold * pot + (1-all_fold)*(equity*(pot+pot*bet_frac)-pot*bet_frac);
            if (bluff_ev > equity * pot) {
                return "RAISE " + std::to_string(safe_raise(bet_frac));
            }
        }
        return "CHECK";
    }
    // facing a bet (deep)
    if (equity > 0.75f + risk_adjust && spr > 2.0f) {
        int r = safe_raise(0.65f);
        if (r >= chips + my_bet) return "ALLIN";
        return "RAISE " + std::to_string(r);
    }
    if (equity > pot_odds + 0.03f) {
        if (to_call >= chips) return "ALLIN";
        return "CALL";
    }
    if (equity > pot_odds - 0.08f && spr > 6.0f && equity > 0.25f) {
        if (to_call >= chips) return "ALLIN";
        return "CALL";
    } 
    return "FOLD";
}


int main() {
    GameState gs;
    std::string line;

    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        if (cmd == "GAME_START") {
            iss >> gs.num_players >> gs.my_seat >> gs.starting_chips
            >> gs.swap_mult[0] >> gs.swap_mult[1] >> 
            gs.swap_mult[2] >> gs.swap_mult[3];
            gs.chips.assign(gs.num_players, gs.starting_chips);
            gs.opp_stats.resize(gs.num_players);
            gs.folded.assign(gs.num_players, false);
            gs.all_in_flag.assign(gs.num_players, false);
            gs.pot_estimate = 0;
        }

        else if (cmd == "HAND_START") {
            iss >> gs.hand_num >> gs.dealer
            >> gs.sb_seat >> gs.bb_seat
            >> gs.sb_amount >> gs.bb_amount;
            gs.community.clear();
            gs.discarded.clear();
            gs.street = 0;
            gs.folded.assign(gs.num_players, false);
            gs.all_in_flag.assign(gs.num_players, false);
            gs.pot_estimate = gs.sb_amount + gs.bb_amount;
            gs.swaps_this_phase = 0;
            for (int i = 0; i < gs.num_players; i++) {
                if (i != gs.my_seat && !gs.opp_stats[i].eliminated)
                    gs.opp_stats[i].hands_seen++;
            }
        }

        else if (cmd == "CHIPS") {
            for (int i = 0; i < gs.num_players; i++) {
                iss >> gs.chips[i];
            }
        }

        else if (cmd == "DEAL_HOLE") {
            std::string c1, c2;
            iss >> c1 >> c2;
            gs.hole[0] = parse_card(c1);
            gs.hole[1] = parse_card(c2);
        }

        else if (cmd == "DEAL_FLOP") {
            gs.street = 1;
            std::string c1, c2, c3;
            iss >> c1 >> c2 >> c3;
            gs.community.push_back(parse_card(c1));
            gs.community.push_back(parse_card(c2));
            gs.community.push_back(parse_card(c3));
        }

        else if (cmd == "DEAL_TURN") {
            gs.street = 2;
            std::string c;
            iss >> c;
            gs.community.push_back(parse_card(c));
        }

        else if (cmd == "DEAL_RIVER") {
            gs.street = 3;
            std::string c;
            iss >> c;
            gs.community.push_back(parse_card(c));
        }

        // handle redraws
        else if (cmd == "REDRAW_FLOP") {
            for (auto& c : gs.community) {
                gs.discarded.push_back(c);
            }
            gs.community.clear();
            std::string c1, c2, c3;
            iss >> c1 >> c2 >> c3;
            gs.community.push_back(parse_card(c1));
            gs.community.push_back(parse_card(c2));
            gs.community.push_back(parse_card(c3));
        }

        else if (cmd == "REDRAW_TURN") {
            while (gs.community.size() > 3) {
                gs.discarded.push_back(gs.community.back());
                gs.community.pop_back();
            }
            std::string c;
            iss >> c;
            gs.community.push_back(parse_card(c));
        }
        
        else if (cmd == "REDRAW_RIVER") {
            while (gs.community.size() > 4) {
                gs.discarded.push_back(gs.community.back());
                gs.community.pop_back();
            }
            std::string c;
            iss >> c;
            gs.community.push_back(parse_card(c));
        }
        
        // handle decisions
        else if (cmd == "SWAP_PROMPT") {
            int cost, my_chips;
            iss >> cost >> my_chips;
            gs.chips[gs.my_seat] = my_chips;
            std::string resp = decide_swap(gs, cost, my_chips);
            std::cout << resp << std::endl;
            // track the discarded card and pot, if swapping
            if (resp.size() >= 6 && resp.substr(0, 4) == "SWAP") {
                int idx = resp[5] - '0';

                gs.discarded.push_back(gs.hole[idx]);
                gs.pot_estimate += cost;
                gs.swaps_this_phase++;
            }
        }

        else if (cmd == "SWAP_RESULT") {
            std::string c;
            iss >> c;
            Card new_card = parse_card(c);
            // replace the card we swapped
            if (!gs.discarded.empty()) {
                Card old = gs.discarded.back();
                if (gs.hole[0] == old) gs.hole[0] = new_card;
                else gs.hole[1] = new_card;
            }
        }

        else if (cmd == "SWAP_DONE") {
            gs.swaps_this_phase = 0;
        }

        else if (cmd == "VOTE_PROMPT") {
            int my_chips;
            iss >> my_chips;
            gs.chips[gs.my_seat] = my_chips;
            std::cout << decide_vote(gs, my_chips) << std::endl;
        }

        else if (cmd == "VOTE_RESULT") {
            int yes_total, no_total;
            std::string outcome;
            iss >> yes_total >> no_total >> outcome;
            gs.pot_estimate += yes_total + no_total;
        }

        else if (cmd == "ACTION_PROMPT") {
            int chips, current_bet, my_bet, min_raise, pot;
            iss >> chips >> current_bet >> my_bet >> min_raise >> pot;
            gs.chips[gs.my_seat] = chips;
            gs.pot_estimate = pot; // use actual pot from engine for better accuracy
            std::cout << decide_action(gs, chips, current_bet, my_bet, min_raise, pot) << std::endl;
        }

        else if (cmd == "ACTION") {
            int seat;
            std::string action;
            iss >> seat >> action;
            if (action == "FOLD") {
                gs.folded[seat] = true;
                if (seat != gs.my_seat) {
                    gs.opp_stats[seat].fold_count++;
                    gs.opp_stats[seat].action_count++;
                }
            }
            else if (action == "CHECK") {
                if (seat != gs.my_seat) {
                    gs.opp_stats[seat].total_calls_checks++;
                    gs.opp_stats[seat].action_count++;
                }
            }
            else if (action == "CALL") {
                int amt = 0;
                iss >> amt;
                gs.pot_estimate += amt;
                if (seat != gs.my_seat) {
                    gs.opp_stats[seat].total_calls_checks++;
                    gs.opp_stats[seat].action_count++;
                    if (gs.street == 0) gs.opp_stats[seat].vpip_count++;
                }
            }
            else if (action == "RAISE" || action == "ALLIN") {
                int amt = 0;
                iss >> amt;
                if (action == "ALLIN") gs.all_in_flag[seat] = true;
                gs.pot_estimate += amt;
                if (seat != gs.my_seat) {
                    gs.opp_stats[seat].total_bets_raises++;
                    gs.opp_stats[seat].action_count++;
                    if (gs.street == 0) {
                        gs.opp_stats[seat].vpip_count++;
                        gs.opp_stats[seat].pfr_count++;
                    }
                }
            }
        }

        else if (cmd == "ELIMINATE") {
            int seat;
            iss >> seat;
            gs.opp_stats[seat].eliminated = true;
        }
        else if (cmd == "GAME_OVER") {
            break;
        }

        // ignore other messages like SHOWDOWN, WINNER, etc. for now
    }
}