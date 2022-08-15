#include "ProductionQueue.h"

#include "Empire.h"
#include "../universe/BuildingType.h"
#include "../universe/Condition.h"
#include "../universe/Planet.h"
#include "../universe/ShipHull.h"
#include "../universe/ShipPart.h"
#include "../universe/ShipDesign.h"
#include "../universe/ValueRef.h"
#include "../util/AppInterface.h"
#include "../util/GameRules.h"
#include "../util/ScopedTimer.h"
#include "../util/i18n.h"

#include <boost/range/numeric.hpp>
#include <boost/range/adaptor/map.hpp>
#include <boost/uuid/uuid_io.hpp>


namespace {
    constexpr float EPSILON = 0.001f;

    void AddRules(GameRules& rules) {
        // limits amount of PP per turn that can be imported into the stockpile
        rules.Add<bool>(UserStringNop("RULE_STOCKPILE_IMPORT_LIMITED"),
                        UserStringNop("RULE_STOCKPILE_IMPORT_LIMITED_DESC"),
                        "", false, true);

        rules.Add<double>(UserStringNop("RULE_PRODUCTION_QUEUE_FRONTLOAD_FACTOR"),
                          UserStringNop("RULE_PRODUCTION_QUEUE_FRONTLOAD_FACTOR_DESC"),
                          "", 0.0, true, RangedValidator<double>(0.0, 30.0));
        rules.Add<double>(UserStringNop("RULE_PRODUCTION_QUEUE_TOPPING_UP_FACTOR"),
                          UserStringNop("RULE_PRODUCTION_QUEUE_TOPPING_UP_FACTOR_DESC"),
                          "", 0.0, true, RangedValidator<double>(0.0, 30.0));
    }
    bool temp_bool = RegisterGameRules(&AddRules);

    // Calculates per-turn limit on PP contribution, taking into account unit
    // item cost, min build turns, blocksize, remaining repeat count, current
    // progress, and other potential factors discussed below.

    // RULE_PRODUCTION_QUEUE_FRONTLOAD_FACTOR and
    // RULE_PRODUCTION_QUEUE_TOPPING_UP_FACTOR specify how the ProductionQueue
    // will limit allocation towards building a given item on a given turn.
    // The base amount of maximum allocation per turn (if the player has enough
    // PP available) is the item's total cost, divided over its minimum build
    // time.  Sometimes complications arise, though, which unexpectedly delay
    // the completion even if the item had been fully-funded every turn,
    // because costs have risen partway through (such as due to increasing ship
    // costs resulting from recent ship constructoin completion and ensuing
    // increase of Fleet Maintenance costs.
    // These two settings provide a mechanism for some allocation leeway to deal
    // with mid-build cost increases without causing the project  completion to
    // take an extra turn because of the small bit of increased cost.  The
    // settings differ in the timing of the extra allocation allowed.
    // Both factors have a minimum value of 0.0 and a maximum value of 0.3.

    // Making the frontloaded factor greater than zero increases the per-turn
    // allocation cap by the specified percentage (so it always spreads the
    // extra allocation across all turns). Making the topping-up option nonzero
    // allows the final turn allocation cap to be increased by the specified
    // percentage of the total cost, if needed (and then subject toavailability
    // of course). They can both be nonzero, although to avoid that introducing
    // too much interaction complexity into the minimum build time safeguard for
    // topping-up,  the topping-up percentage will be reduced by the
    // frontloading setting.

    // Note that for very small values of the options (less than 5%), when
    // dealing with very low cost items the effect/protection may be noticeably
    // less than expected because of interactions with the ProductionQueue
    // Epsilon value (0.01)

    float CalculateProductionPerTurnLimit(const ProductionQueue::Element& queue_element,
                                          float item_cost, int build_turns)
    {
        const float frontload_limit_factor = static_cast<float>(
            GetGameRules().Get<double>("RULE_PRODUCTION_QUEUE_FRONTLOAD_FACTOR") * 0.01);
        // any allowed topping up is limited by how much frontloading was allowed
        const float topping_up_limit_factor = static_cast<float>(
            std::max(0.0, GetGameRules().Get<double>("RULE_PRODUCTION_QUEUE_TOPPING_UP_FACTOR") * 0.01 - frontload_limit_factor));

        item_cost *= queue_element.blocksize;
        build_turns = std::max(build_turns, 1);
        float element_accumulated_PP = queue_element.progress*item_cost;            // effective PP accumulated by this element towards producing next item. progress is a fraction between 0 and 1.
        float element_total_cost = item_cost * queue_element.remaining;             // total PP to produce all items in this element
        float additional_pp_to_complete_element =
            element_total_cost - element_accumulated_PP;                            // additional PP, beyond already-accumulated PP, to produce all items in this element
        float additional_pp_to_complete_item = item_cost - element_accumulated_PP;  // additional PP, beyond already-accumulated PP, to produce the current item of this element
        float basic_element_per_turn_limit = item_cost / build_turns;
        // the extra constraints on frontload and topping up amounts ensure that won't let complete in less than build_turns (so long as costs do not decrease)
        float frontload = (1.0f + frontload_limit_factor/std::max(build_turns-1,1)) *
            basic_element_per_turn_limit - 2.0f * EPSILON;
        float topping_up_limit = basic_element_per_turn_limit +
            std::min(topping_up_limit_factor * item_cost, basic_element_per_turn_limit - 2 * EPSILON);
        float topping_up = (additional_pp_to_complete_item < topping_up_limit) ?
            additional_pp_to_complete_item : basic_element_per_turn_limit;
        float retval = std::min(additional_pp_to_complete_element,
                                std::max(basic_element_per_turn_limit,
                                         std::max(frontload, topping_up)));
        //DebugLogger() << "CalculateProductionPerTurnLimit for item " << queue_element.item.build_type << " " << queue_element.item.name 
        //              << " " << queue_element.item.design_id << " :  accumPP: " << element_accumulated_PP << " pp_to_complete_elem: "
        //              << additional_pp_to_complete_element << " pp_to_complete_item: " << additional_pp_to_complete_item 
        //              <<  " basic_element_per_turn_limit: " << basic_element_per_turn_limit << " frontload: " << frontload
        //              << " topping_up_limit: " << topping_up_limit << " topping_up: " << topping_up << " retval: " << retval;

        return retval;
    }

    float CalculateNewStockpile(int empire_id, float starting_stockpile,
                                float project_transfer_to_stockpile,
                                const std::map<std::set<int>, float>& available_pp,
                                const std::map<std::set<int>, float>& allocated_pp,
                                const std::map<std::set<int>, float>& allocated_stockpile_pp,
                                const ScriptingContext& context)
    {
        TraceLogger() << "CalculateNewStockpile for empire " << empire_id;
        auto empire = context.GetEmpire(empire_id);
        if (!empire) {
            ErrorLogger() << "CalculateStockpileContribution() passed null empire.  doing nothing.";
            return 0.0f;
        }
        float stockpile_limit = empire->GetProductionQueue().StockpileCapacity(context.ContextObjects());
        float stockpile_used = boost::accumulate(allocated_stockpile_pp | boost::adaptors::map_values, 0.0f);
        TraceLogger() << " ... stockpile limit: " << stockpile_limit << "  used: " << stockpile_used << "   starting: " << starting_stockpile;
        float new_contributions = 0.0f;
        for (auto const& available_group : available_pp) {
            auto alloc_it = allocated_pp.find(available_group.first);
            float allocated_here = (alloc_it == allocated_pp.end())
                ? 0.0f : alloc_it->second;
            float excess_here = available_group.second - allocated_here;
            if (excess_here < EPSILON)
                continue;
            // Transfer excess to stockpile
            new_contributions += excess_here;
            TraceLogger() << "...allocated in group: " << allocated_here
                          << "  excess in group: " << excess_here
                          << "  to stockpile: " << new_contributions;
        }

        if ((new_contributions + project_transfer_to_stockpile) > stockpile_limit &&
            GetGameRules().Get<bool>("RULE_STOCKPILE_IMPORT_LIMITED"))
        { new_contributions = stockpile_limit - project_transfer_to_stockpile; }

        return starting_stockpile + new_contributions + project_transfer_to_stockpile - stockpile_used;
    }

    /** Sets the allocated_pp value for each Element in the passed
      * ProductionQueue \a queue.  Elements are allocated PP based on their need,
      * the limits they can be given per turn, and the amount available at their
      * production location (which is itself limited by the resource supply
      * system groups that are able to exchange resources with the build
      * location and the amount of minerals and industry produced in the group).
      * Elements will not receive funding if they cannot be produced by the
      * empire with the indicated \a empire_id this turn at their build location. 
      * Also checks if elements will be completed this turn. 
      * Returns the amount of PP which gets transferred to the stockpile using 
      * stockpile project build items. */
    float SetProdQueueElementSpending(
        std::map<std::set<int>, float> available_pp, float available_stockpile,
        float stockpile_limit,
        const std::vector<std::set<int>>& queue_element_resource_sharing_object_groups,
        const std::map<std::pair<ProductionQueue::ProductionItem, int>,
                       std::pair<float, int>>& queue_item_costs_and_times,
        const std::vector<bool>& is_producible,
        ProductionQueue::QueueType& queue,
        std::map<std::set<int>, float>& allocated_pp,
        std::map<std::set<int>, float>& allocated_stockpile_pp,
        int& projects_in_progress, bool simulating,
        const Universe& universe)
    {
        //DebugLogger() << "========SetProdQueueElementSpending========";
        //DebugLogger() << "production status: ";
        //DebugLogger() << "queue: ";
        //for (const ProductionQueue::Element& elem : queue)
        //    DebugLogger() << " ... name: " << elem.item.name << "id: " << elem.item.design_id << " allocated: " << elem.allocated_pp << " locationid: " << elem.location << " ordered: " << elem.ordered;

        if (queue.size() != queue_element_resource_sharing_object_groups.size()) {
            ErrorLogger() << "SetProdQueueElementSpending queue size and sharing groups size inconsistent. aborting";
            return 0.0f;
        }

        // See explanation at CalculateProductionPerTurnLimit() above regarding operation of these factors.
        // any allowed topping up is limited by how much frontloading was allowed
        //const float frontload_limit_factor = GetGameRules().Get<double>("RULE_PRODUCTION_QUEUE_FRONTLOAD_FACTOR") * 0.01;
        //const float topping_up_limit_factor = std::max(0.0, GetGameRules().Get<double>("RULE_PRODUCTION_QUEUE_TOPPING_UP_FACTOR") * 0.01f - frontload_limit_factor);
        // DebugLogger() << "SetProdQueueElementSpending frontload  factor " << frontload_limit_factor;
        // DebugLogger() << "SetProdQueueElementSpending topping up factor " << topping_up_limit_factor;

        projects_in_progress = 0;
        allocated_pp.clear();
        allocated_stockpile_pp.clear();
        float dummy_pp_source = 0.0f;
        float stockpile_transfer = 0.0f;
        //DebugLogger() << "queue size: " << queue.size();
        int i = 0;
        for (auto& queue_element : queue) {
            queue_element.allocated_pp = 0.0f;  // default, to be updated below...
            if (queue_element.paused) {
                TraceLogger() << "allocation: " << queue_element.allocated_pp
                              << "  to: " << queue_element.item.name
                              << "  due to it being paused";
                ++i;
                continue;
            }

            // get resource sharing group and amount of resource available to build this item
            const auto& group = queue_element_resource_sharing_object_groups[i];
            auto available_pp_it = available_pp.find(group);
            float& group_pp_available = (available_pp_it != available_pp.end()) ?
                                        available_pp_it->second : dummy_pp_source;

            if ((group_pp_available <= 0) &&
                (available_stockpile <= 0 || !queue_element.allowed_imperial_stockpile_use))
            {
                TraceLogger() << "allocation: " << queue_element.allocated_pp
                              << "  to: " << queue_element.item.name
                              << "  due to lack of available PP in group";
                queue_element.allocated_pp = 0.0f;
                ++i;
                continue;
            }

            //DebugLogger() << "group has " << group_pp_available << " PP available";

            // see if item is producible this turn...
            if (!is_producible[i]) {
                // can't be produced at this location this turn.
                queue_element.allocated_pp = 0.0f;
                TraceLogger() << "allocation: " << queue_element.allocated_pp
                              << "  to unproducible item: " << queue_element.item.name;
                ++i;
                continue;
            }

            // get max contribution per turn and turns to build at max contribution rate
            int location_id = (queue_element.item.CostIsProductionLocationInvariant(universe) ?
                INVALID_OBJECT_ID : queue_element.location);
            std::pair<ProductionQueue::ProductionItem, int> key(queue_element.item, location_id);
            float item_cost = 1e6;  // dummy/default value, shouldn't ever really be needed
            int build_turns = 1;    // dummy/default value, shouldn't ever really be needed
            auto time_cost_it = queue_item_costs_and_times.find(key);
            if (time_cost_it != queue_item_costs_and_times.end()) {
                item_cost = time_cost_it->second.first;
                build_turns = time_cost_it->second.second;
            } else {
                ErrorLogger() << "item: " << queue_element.item.name
                              << "  somehow failed time cost lookup for location " << location_id;
            }
            //DebugLogger() << "item " << queue_element.item.name << " costs " << item_cost << " for " << build_turns << " turns";

            float element_this_turn_limit = CalculateProductionPerTurnLimit(
                queue_element, item_cost, build_turns);

            // determine how many pp to allocate to this queue element block this turn.  allocation is limited by the
            // item cost, which is the max number of PP per turn that can be put towards this item, and by the
            // total cost remaining to complete the last item in the queue element (eg. the element has all but
            // the last item complete already) and by the total pp available in this element's production location's
            // resource sharing group (including any stockpile availability)
            float stockpile_available_for_this =
                (queue_element.allowed_imperial_stockpile_use) ? available_stockpile : 0;

            float allocation = std::max(0.0f,
                std::min(element_this_turn_limit,
                         group_pp_available + stockpile_available_for_this));

            if (queue_element.item.build_type == BuildType::BT_STOCKPILE) {
                if (GetGameRules().Get<bool>("RULE_STOCKPILE_IMPORT_LIMITED")) {
                    float unused_limit = std::max(0.0f, stockpile_limit - stockpile_transfer);
                    allocation = std::min(allocation, unused_limit);
                }
            }

            //DebugLogger() << "element accumulated " << element_accumulated_PP << " of total cost "
            //                       << element_total_cost << " and needs " << additional_pp_to_complete_element
            //                       << " more to be completed";
            //DebugLogger() << "... allocating " << allocation;

            // allocate pp
            queue_element.allocated_pp = std::max(allocation, EPSILON);

            // record allocation from group
            float group_drawdown = std::min(allocation, group_pp_available);

            allocated_pp[group] += group_drawdown;  // relies on default initial mapped value of 0.0f
            if (queue_element.item.build_type == BuildType::BT_STOCKPILE)
                stockpile_transfer += group_drawdown;
            group_pp_available -= group_drawdown;

            float stockpile_drawdown = allocation <= group_drawdown ? 0.0f : (allocation - group_drawdown);
            TraceLogger() << "allocation: " << allocation
                          << "  to: " << queue_element.item.name
                          << "  from group: " << group_drawdown
                          << "  from stockpile: " << stockpile_drawdown
                          << "  to stockpile:" << stockpile_transfer
                          << "  group remaining: " << group_pp_available;

            // record allocation from stockpile
            // protect against any slight mismatch that might possible happen from multiplying
            // and dividing by a very very small stockpile_conversion_rate
            stockpile_drawdown = std::min(stockpile_drawdown, available_stockpile);
            if (stockpile_drawdown > 0) {
                allocated_stockpile_pp[group] += stockpile_drawdown;
                available_stockpile -= stockpile_drawdown;
            }

            // check for completion
            float block_cost = item_cost * queue_element.blocksize;
            if (block_cost*(1.0f - queue_element.progress) - queue_element.allocated_pp < EPSILON)
                queue_element.turns_left_to_next_item = 1;

            // if simulating, update progress
            if (simulating)
                queue_element.progress += allocation / std::max(EPSILON, block_cost);    // add turn's progress due to allocation

            if (allocation > 0.0f)
                ++projects_in_progress;

            ++i;
        }
        return stockpile_transfer;
    }
}


ProductionQueue::ProductionItem::ProductionItem(BuildType build_type_) :
    build_type(build_type_)
{
    if (build_type_ == BuildType::BT_STOCKPILE)
        name = UserStringNop("PROJECT_BT_STOCKPILE");
}

ProductionQueue::ProductionItem::ProductionItem(BuildType build_type_, std::string name_) :
    build_type(build_type_),
    name(name_)
{}

ProductionQueue::ProductionItem::ProductionItem(BuildType build_type_, int design_id_, const Universe& universe) :
    build_type(build_type_),
    design_id(design_id_)
{
    if (build_type == BuildType::BT_SHIP) {
        if (const ShipDesign* ship_design = universe.GetShipDesign(design_id))
            name = ship_design->Name();
        else
            ErrorLogger() << "ProductionItem::ProductionItem couldn't get ship design with id: " << design_id;
    }
}

bool ProductionQueue::ProductionItem::CostIsProductionLocationInvariant(const Universe& universe) const {
    if (build_type == BuildType::BT_BUILDING) {
        const BuildingType* type = GetBuildingType(name);
        if (!type)
            return true;
        return type->ProductionCostTimeLocationInvariant();

    } else if (build_type == BuildType::BT_SHIP) {
        const ShipDesign* design = universe.GetShipDesign(design_id);
        if (!design)
            return true;
        return design->ProductionCostTimeLocationInvariant();

    } else if (build_type == BuildType::BT_STOCKPILE) {
        return true;
    }
    return false;
}

std::pair<float, int> ProductionQueue::ProductionItem::ProductionCostAndTime(
    int empire_id, int location_id, const ScriptingContext& context) const
{
    if (build_type == BuildType::BT_BUILDING) {
        const BuildingType* type = GetBuildingType(name);
        if (!type)
            return {-1.0, -1};
        return {type->ProductionCost(empire_id, location_id, context),
                type->ProductionTime(empire_id, location_id, context)};

    } else if (build_type == BuildType::BT_SHIP) {
        const ShipDesign* design = context.ContextUniverse().GetShipDesign(design_id);
        if (design)
            return {design->ProductionCost(empire_id, location_id),
                    design->ProductionTime(empire_id, location_id)};
        return {-1.0, -1};

    } else if (build_type == BuildType::BT_STOCKPILE) {
        return {1.0, 1};
    }
    ErrorLogger() << "Empire::ProductionCostAndTime was passed a ProductionItem with an invalid BuildType";
    return {-1.0, -1};
}

bool ProductionQueue::ProductionItem::EnqueueConditionPassedAt(int location_id, const ScriptingContext& context) const {
    switch (build_type) {
    case BuildType::BT_BUILDING: {
        if (const BuildingType* bt = GetBuildingType(name)) {
            auto c = bt->EnqueueLocation();
            if (!c)
                return true;
            auto location_obj = context.ContextObjects().getRaw(location_id);
            const ScriptingContext location_context(location_obj, context);
            return c->Eval(location_context, location_obj);
        }
        return true;
        break;
    }
    case BuildType::BT_SHIP:   // ships don't have enqueue location conditions
    case BuildType::BT_STOCKPILE:  // stockpile can always be enqueued 
    default:
        return true;
    }
}

bool ProductionQueue::ProductionItem::operator<(const ProductionItem& rhs) const {
    if (build_type < rhs.build_type)
        return true;
    if (build_type > rhs.build_type)
        return false;
    if (build_type == BuildType::BT_BUILDING)
        return name < rhs.name;
    else if (build_type == BuildType::BT_SHIP)
        return design_id < rhs.design_id;

    return false;
}

std::map<std::string, std::map<int, float>>
ProductionQueue::ProductionItem::CompletionSpecialConsumption(int location_id, const ScriptingContext& context) const {
    std::map<std::string, std::map<int, float>> retval;

    switch (build_type) {
    case BuildType::BT_BUILDING: {
        if (const BuildingType* bt = GetBuildingType(name)) {
            auto location_obj = context.ContextObjects().getRaw(location_id);
            ScriptingContext location_target_context{location_obj, context}; // non-const but should be OK as only passed below to function taking const ScriptingContext&

            for (const auto& [special_name, consumption] : bt->ProductionSpecialConsumption()) {
                const auto& [amount, cond] = consumption;
                if (!amount)
                    continue;
                Condition::ObjectSet matches;
                // if a condition selecting where to take resources from was specified, use it.
                // Otherwise take from the production location
                if (cond)
                    matches = cond->Eval(std::as_const(location_target_context));
                else
                    matches.push_back(location_obj);

                // determine how much to take from each matched object
                for (auto* object : matches) {
                    location_target_context.effect_target = const_cast<UniverseObject*>(object); // call to ValueRef cannot modify the pointed-to object
                    retval[special_name][object->ID()] += static_cast<float>(amount->Eval(location_target_context));
                }
            }
        }
        break;
    }
    case BuildType::BT_SHIP: {
        if (const ShipDesign* sd = context.ContextUniverse().GetShipDesign(design_id)) {
            auto location_obj = context.ContextObjects().getRaw(location_id);
            const ScriptingContext location_target_context{location_obj, context};

            if (const ShipHull* ship_hull = GetShipHull(sd->Hull())) {
                for (const auto& [special_name, consumption] : ship_hull->ProductionSpecialConsumption()) {
                    const auto& [amount, cond] = consumption;
                    (void)cond;
                    if (amount)
                        retval[special_name][location_id] += static_cast<float>(amount->Eval(location_target_context));
                }
            }

            for (const std::string& part_name : sd->Parts()) {
                const ShipPart* part = GetShipPart(part_name);
                if (!part)
                    continue;
                for (const auto& [special_name, consumption] : part->ProductionSpecialConsumption()) {
                    const auto& [amount, cond] = consumption;
                    (void)cond;
                    if (amount)
                        retval[special_name][location_id] += static_cast<float>(amount->Eval(location_target_context));
                }
            }
        }
        break;
    }
    case BuildType::BT_PROJECT:    // TODO
    case BuildType::BT_STOCKPILE:  // stockpile transfer consumes no special
    default:
        break;
    }

    return retval;
}

std::map<MeterType, std::map<int, float>>
ProductionQueue::ProductionItem::CompletionMeterConsumption(
    int location_id, const ScriptingContext& context) const
{
    std::map<MeterType, std::map<int, float>> retval;

    const ScriptingContext location_context{context.ContextObjects().getRaw(location_id), context};

    switch (build_type) {
    case BuildType::BT_BUILDING: {
        if (const BuildingType* bt = GetBuildingType(name)) {
            for (const auto& [mt, consumption] : bt->ProductionMeterConsumption()) {
                const auto& [amount, cond] = consumption;
                (void)cond;
                if (amount)
                    retval[mt][location_id] = static_cast<float>(amount->Eval(location_context));
            }
        }
        break;
    }
    case BuildType::BT_SHIP: {
        if (const ShipDesign* sd = context.ContextUniverse().GetShipDesign(design_id)) {
            if (const ShipHull* ship_hull = GetShipHull(sd->Hull())) {
                for (const auto& [mt, consumption] : ship_hull->ProductionMeterConsumption()) {
                    const auto& [amount, cond] = consumption;
                    (void)cond;
                    if (amount)
                        retval[mt][location_id] += static_cast<float>(amount->Eval(location_context));
                }
            }

            for (const std::string& part_name : sd->Parts()) {
                const ShipPart* pt = GetShipPart(part_name);
                if (!pt)
                    continue;
                for (const auto& [mt, consumption] : pt->ProductionMeterConsumption()) {
                    const auto& [amount, cond] = consumption;
                    (void)cond;
                    if (amount)
                        retval[mt][location_id] += static_cast<float>(amount->Eval(location_context));
                }
            }
        }
        break;
    }
    case BuildType::BT_PROJECT:    // TODO
    case BuildType::BT_STOCKPILE:  // stockpile transfer happens before completion - nothing to do
    default:
        break;
    }

    return retval;
}

std::string ProductionQueue::ProductionItem::Dump() const {
    std::string retval{"ProductionItem: "};
    retval.append(to_string(build_type));
    if (!name.empty())
        retval.append(" name: ").append(name);
    if (design_id != INVALID_DESIGN_ID)
        retval.append(" id: ").append(std::to_string(design_id));
    return retval;
}


//////////////////////////////
// ProductionQueue::Element //
//////////////////////////////
ProductionQueue::Element::Element(ProductionItem item_, int empire_id_,
                                  boost::uuids::uuid uuid_, int ordered_,
                                  int remaining_, int blocksize_, int location_, bool paused_,
                                  bool allowed_imperial_stockpile_use_) :
    item(item_),
    empire_id(empire_id_),
    ordered(ordered_),
    blocksize(blocksize_),
    remaining(remaining_),
    location(location_),
    blocksize_memory(blocksize_),
    paused(paused_),
    allowed_imperial_stockpile_use(allowed_imperial_stockpile_use_),
    uuid(uuid_)
{}

std::pair<float, int> ProductionQueue::Element::ProductionCostAndTime(const ScriptingContext& context) const
{ return item.ProductionCostAndTime(empire_id, location, context); }

std::string ProductionQueue::Element::Dump() const {
    std::string retval = "ProductionQueue::Element (" + item.Dump() + ") (" +
        std::to_string(blocksize) + ") x" + std::to_string(ordered) + " ";
    retval += " (remaining: " + std::to_string(remaining) + ")  uuid: " + boost::uuids::to_string(uuid);
    return retval;
}


/////////////////////
// ProductionQueue //
/////////////////////
ProductionQueue::ProductionQueue(int empire_id) :
    m_empire_id(empire_id)
{}

int ProductionQueue::ProjectsInProgress() const
{ return m_projects_in_progress; }

float ProductionQueue::TotalPPsSpent() const {
    // add up allocated PP from all resource sharing object groups
    float retval = 0.0f;
    for (const auto& entry : m_object_group_allocated_pp)
        retval += entry.second;
    return retval;
}

const std::map<std::set<int>, float>& ProductionQueue::AllocatedPP() const
{ return m_object_group_allocated_pp; }

const std::map<std::set<int>, float>& ProductionQueue::AllocatedStockpilePP() const
{ return m_object_group_allocated_stockpile_pp; }

float ProductionQueue::StockpileCapacity(const ObjectMap& objects) const {
    if (m_empire_id == ALL_EMPIRES)
        return 0.0f;

    float retval = 0.0f;

    // TODO: if something other than planets has METER_STOCKPILE added, adjust here
    auto owned_planets = objects.find<Planet>([empire_id{m_empire_id}](const Planet* p)
                                              { return p->OwnedBy(empire_id); });
    for (const auto& obj : owned_planets) {
        if (const auto* meter = obj->GetMeter(MeterType::METER_STOCKPILE))
            retval += meter->Current();
    }
    return retval;
}

std::set<std::set<int>> ProductionQueue::ObjectsWithWastedPP(const std::shared_ptr<const ResourcePool>& industry_pool) const {
    std::set<std::set<int>> retval;
    if (!industry_pool) {
        ErrorLogger() << "ProductionQueue::ObjectsWithWastedPP passed invalid industry resource pool";
        return retval;
    }

    for (auto& [group, avail_pp_in_group] : industry_pool->Output()) {
        //std::cout << "available PP groups size: " << avail_pp.first.size() << " pp: " << avail_pp.second << std::endl;

        if (avail_pp_in_group <= 0)
            continue;   // can't waste if group has no PP

        // find this group's allocated PP
        auto alloc_it = m_object_group_allocated_pp.find(group);
        // is less allocated than is available?  if so, some is wasted (assumes stockpile contribuutions can never be lossless)
        // XXX maybe should check stockpile input ratio
        if (alloc_it == m_object_group_allocated_pp.end() || alloc_it->second < avail_pp_in_group)
            retval.insert(group);
    }
    return retval;
}

bool ProductionQueue::empty() const
{ return !m_queue.size(); }

unsigned int ProductionQueue::size() const
{ return m_queue.size(); }

ProductionQueue::const_iterator ProductionQueue::begin() const
{ return m_queue.begin(); }

ProductionQueue::const_iterator ProductionQueue::end() const
{ return m_queue.end(); }

ProductionQueue::const_iterator ProductionQueue::find(int i) const
{ return (0 <= i && i < static_cast<int>(size())) ? (begin() + i) : end(); }

const ProductionQueue::Element& ProductionQueue::operator[](int i) const {
    if (i < 0 || i >= static_cast<int>(m_queue.size()))
        throw std::out_of_range("Tried to access ProductionQueue element out of bounds");
    return m_queue[i];
}

ProductionQueue::const_iterator ProductionQueue::find(boost::uuids::uuid uuid) const {
    if (uuid == boost::uuids::nil_generator()())
        return m_queue.end();
    for (auto it = m_queue.begin(); it != m_queue.end(); ++it)
        if (it->uuid == uuid)
            return it;
    return m_queue.end();
}

int ProductionQueue::IndexOfUUID(boost::uuids::uuid uuid) const {
    auto it = find(uuid);
    if (it == end())
        return -1;
    return std::distance(begin(), it);
}

void ProductionQueue::Update(const ScriptingContext& context) {
    const Universe& universe{context.ContextUniverse()};

    auto empire = context.GetEmpire(m_empire_id);
    if (!empire) {
        ErrorLogger() << "ProductionQueue::Update passed null empire id.  doing nothing.";
        m_projects_in_progress = 0;
        m_object_group_allocated_pp.clear();
        return;
    }

    SectionedScopedTimer update_timer("ProductionQueue::Update");
    update_timer.EnterSection("Get PP");

    auto industry_resource_pool = empire->GetResourcePool(ResourceType::RE_INDUSTRY);
    auto& available_pp = industry_resource_pool->Output();
    float pp_in_stockpile = industry_resource_pool->Stockpile();
    TraceLogger() << "========= pp_in_stockpile:     " << pp_in_stockpile << " ========";
    float stockpile_limit = StockpileCapacity(context.ContextObjects());
    float available_stockpile = std::min(pp_in_stockpile, stockpile_limit);
    TraceLogger() << "========= available_stockpile: " << available_stockpile << " ========";

    update_timer.EnterSection("Queue Items -> Res Groups");
    // determine which resource sharing group each queue item is located in
    std::vector<std::set<int>> queue_element_groups;
    for (const auto& element : m_queue) {
        // get location object for element
        int location_id = element.location;

        // search through groups to find object
        for (auto groups_it = available_pp.begin(); true; ++groups_it) { // TODO: structured binding?
            if (groups_it == available_pp.end()) {
                // didn't find a group containing this object, so add an empty group as this element's queue element group
                queue_element_groups.emplace_back();
                break;
            }

            // check if location object id is in this group
            const auto& group = groups_it->first;
            auto set_it = group.find(location_id);
            if (set_it != group.end()) {
                // system is in this group.
                queue_element_groups.push_back(group); // record this discovery
                break;                                 // stop searching for a group containing a system, since one has been found
            }
        }
    }

    update_timer.EnterSection("Cacheing Costs");
    // cache producibility, and production item costs and times
    // initialize production queue item completion status to 'never'
    std::map<std::pair<ProductionQueue::ProductionItem, int>,
             std::pair<float, int>> queue_item_costs_and_times;
    std::vector<bool> is_producible;
    for (auto& elem : m_queue) {
        is_producible.push_back(empire->ProducibleItem(elem.item, elem.location, context));
        // for items that don't depend on location, only store cost/time once
        int location_id = (elem.item.CostIsProductionLocationInvariant(universe) ?
                           INVALID_OBJECT_ID : elem.location);
        auto key = std::pair{elem.item, location_id};

        if (!queue_item_costs_and_times.count(key))
            queue_item_costs_and_times[key] = elem.ProductionCostAndTime();

        elem.turns_left_to_next_item = -1;
        elem.turns_left_to_completion = -1;
    }

    // duplicate production queue state for future simulation
    QueueType sim_queue = m_queue;
    std::vector<unsigned int> sim_queue_original_indices(sim_queue.size());
    for (unsigned int i = 0; i < sim_queue_original_indices.size(); ++i)
        sim_queue_original_indices[i] = i;

    update_timer.EnterSection("Set Spending");
    // allocate pp to queue elements, returning updated available pp and updated
    // allocated pp for each group of resource sharing objects
    float project_transfer_to_stockpile = SetProdQueueElementSpending(
        available_pp, available_stockpile, stockpile_limit, queue_element_groups,
        queue_item_costs_and_times, is_producible, m_queue,
        m_object_group_allocated_pp, m_object_group_allocated_stockpile_pp,
        m_projects_in_progress, false, universe);

    //update expected new stockpile amount
    m_expected_new_stockpile_amount = CalculateNewStockpile(
        m_empire_id, pp_in_stockpile, project_transfer_to_stockpile,
        available_pp, m_object_group_allocated_pp,
        m_object_group_allocated_stockpile_pp,
        context);
    m_expected_project_transfer_to_stockpile = project_transfer_to_stockpile;

    // if at least one resource-sharing system group have available PP, simulate
    // future turns to predict when build items will be finished
    bool simulate_future = false;
    for (auto& available : available_pp) {
        if (available.second > EPSILON) {
            simulate_future = true;
            break;
        }
    }

    if (!simulate_future) {
        update_timer.EnterSection("Signal and Finish");
        DebugLogger() << "not enough PP to be worth simulating future turns production.  marking everything as never complete";
        ProductionQueueChangedSignal();
        return;
    }

    // there are enough PP available in at least one group to make it worthwhile to simulate the future.
    DebugLogger() << "ProductionQueue::Update: Simulating future turns of production queue";

    constexpr int TOO_MANY_TURNS = 500;   // stop counting turns to completion after this long, to prevent seemingly endless loops
    constexpr float TOO_LONG_TIME = 0.5f; // max time in seconds to spend simulating queue


    // remove from simulated queue any paused items and items that can't be built due to not
    // meeting their location conditions; can't feasibly re-check
    // buildability each projected turn as this would require creating a simulated
    // universe into which simulated completed buildings could be inserted, as
    // well as spoofing the current turn, or otherwise faking the results for
    // evaluating arbitrary location conditions for the simulated universe.
    // this would also be inaccurate anyway due to player choices or random
    // chance, so for simplicity, it is assumed that building location
    // conditions evaluated at the present turn apply indefinitely.
    update_timer.EnterSection("Remove Unproducible");
    for (unsigned int i = 0; i < sim_queue.size(); ++i) {
        if (sim_queue[i].paused || !is_producible[i]) {
            sim_queue.erase(sim_queue.begin() + i);
            is_producible.erase(is_producible.begin() + i);
            queue_element_groups.erase(queue_element_groups.begin() + i);
            sim_queue_original_indices.erase(sim_queue_original_indices.begin() + i--);
        }
    }

    boost::posix_time::ptime sim_time_start;
    boost::posix_time::ptime sim_time_end;
    long sim_time;
    sim_time_start = boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()); 
    std::map<std::set<int>, float>  allocated_pp;
    float sim_available_stockpile = available_stockpile;
    float sim_pp_in_stockpile = pp_in_stockpile;
    std::map<std::set<int>, float>  allocated_stockpile_pp;
    int dummy_int = 0;

    update_timer.EnterSection("Looping over Turns");
    for (int sim_turn = 1; sim_turn <= TOO_MANY_TURNS; sim_turn ++) {
        long sim_time_until_now = (boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time()) - sim_time_start).total_microseconds();
        if ((sim_time_until_now * 1e-6) >= TOO_LONG_TIME)
            break;

        TraceLogger() << "sim turn: " << sim_turn << "  sim queue size: " << sim_queue.size();
        if (sim_queue.empty() && sim_turn > 2)
            break;

        allocated_pp.clear();
        allocated_stockpile_pp.clear();

        float sim_project_transfer_to_stockpile = SetProdQueueElementSpending(
            available_pp, sim_available_stockpile, stockpile_limit, queue_element_groups,
            queue_item_costs_and_times, is_producible, sim_queue,
            allocated_pp, allocated_stockpile_pp, dummy_int, true, universe);

        // check completion status and update m_queue and sim_queue as appropriate
        for (unsigned int i = 0; i < sim_queue.size(); i++) {
            ProductionQueue::Element& sim_element = sim_queue[i];
            ProductionQueue::Element& orig_element = m_queue[sim_queue_original_indices[i]];
            if (sim_element.turns_left_to_next_item != 1) 
                continue;
            sim_element.progress = std::max(0.0f, sim_element.progress - 1.0f);
            if (orig_element.turns_left_to_next_item == -1)
                orig_element.turns_left_to_next_item = sim_turn;
            sim_element.turns_left_to_next_item = -1;

            // if all repeats of item are complete, update completion time and remove from sim_queue
            if (--sim_element.remaining == 0) {
                orig_element.turns_left_to_completion = sim_turn;
                sim_queue.erase(sim_queue.begin() + i);
                is_producible.erase(is_producible.begin() + i);
                queue_element_groups.erase(queue_element_groups.begin() + i);
                sim_queue_original_indices.erase(sim_queue_original_indices.begin() + i--);
            }
        }
        sim_pp_in_stockpile = CalculateNewStockpile(
            m_empire_id, sim_pp_in_stockpile, sim_project_transfer_to_stockpile,
            available_pp, allocated_pp, allocated_stockpile_pp,
            context);
        sim_available_stockpile = std::min(sim_pp_in_stockpile, stockpile_limit);
    }
    update_timer.EnterSection("Logging");

    sim_time_end = boost::posix_time::ptime(boost::posix_time::microsec_clock::local_time());
    sim_time = (sim_time_end - sim_time_start).total_microseconds();
    if ((sim_time * 1e-6) >= TOO_LONG_TIME) {
        DebugLogger()  << "ProductionQueue::Update: Projections timed out after " << sim_time
                       << " microseconds; all remaining items in queue marked completing 'Never'.";
    }
    DebugLogger() << "ProductionQueue::Update: Projections took "
                  << ((sim_time_end - sim_time_start).total_microseconds()) << " microseconds with "
                  << empire->ResourceOutput(ResourceType::RE_INDUSTRY) << " industry output";

    update_timer.EnterSection("ProductionQueueChangedSignal response");
    ProductionQueueChangedSignal();
}

void ProductionQueue::push_back(const Element& element) {
    if (find(element.uuid) != end()) {
        ErrorLogger() << "Trying to push back repeated UUID " << element.uuid;
        throw std::invalid_argument("Repeated use of UUID");
    }
    m_queue.push_back(element);
}

void ProductionQueue::push_back(Element&& element) {
    if (find(element.uuid) != end()) {
        ErrorLogger() << "Trying to push back repeated UUID " << element.uuid;
        throw std::invalid_argument("Repeated use of UUID");
    }
    m_queue.push_back(std::move(element));
}

void ProductionQueue::insert(iterator it, const Element& element) {
    if (find(element.uuid) != end()) {
        ErrorLogger() << "Trying to insert repeated UUID " << element.uuid;
        throw std::invalid_argument("Repeated use of UUID");
    }
    m_queue.insert(it, element);
}

void ProductionQueue::erase(int i) {
    if (i < 0 || i >= static_cast<int>(m_queue.size()))
        throw std::out_of_range("Tried to erase ProductionQueue item out of bounds.");
    m_queue.erase(begin() + i);
}

ProductionQueue::iterator ProductionQueue::erase(iterator it) {
    if (it == end())
        throw std::out_of_range("Tried to erase ProductionQueue item out of bounds.");
    return m_queue.erase(it);
}

ProductionQueue::iterator ProductionQueue::begin()
{ return m_queue.begin(); }

ProductionQueue::iterator ProductionQueue::end()
{ return m_queue.end(); }

ProductionQueue::iterator ProductionQueue::find(int i)
{ return (0 <= i && i < static_cast<int>(size())) ? (begin() + i) : end(); }

ProductionQueue::Element& ProductionQueue::operator[](int i) {
    if (i < 0 || i >= static_cast<int>(m_queue.size()))
        throw std::out_of_range("Tried to access ProductionQueue element out of bounds");
    return m_queue[i];
}

void ProductionQueue::clear() {
    m_queue.clear();
    m_projects_in_progress = 0;
    m_object_group_allocated_pp.clear();
    ProductionQueueChangedSignal();
}
