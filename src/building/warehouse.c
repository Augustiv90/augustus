#include "warehouse.h"

#include "building/barracks.h"
#include "building/count.h"
#include "building/granary.h"
#include "building/industry.h"
#include "building/monument.h"
#include "building/model.h"
#include "building/storage.h"
#include "city/finance.h"
#include "city/resource.h"
#include "core/calc.h"
#include "core/image.h"
#include "empire/trade_prices.h"
#include "figure/figure.h"
#include "game/tutorial.h"
#include "map/image.h"
#include "scenario/property.h"

#define INFINITE 10000

#define MAX_CARTLOADS_PER_SPACE 4

int building_warehouse_get_space_info(building *warehouse)
{
    int total_loads = 0;
    int empty_spaces = 0;
    building *space = warehouse;
    for (int i = 0; i < 8; i++) {
        space = building_next(space);
        if (space->id <= 0) {
            return 0;
        }
        if (space->subtype.warehouse_resource_id) {
            total_loads += space->resources[space->subtype.warehouse_resource_id];
        } else {
            empty_spaces++;
        }
    }
    if (empty_spaces > 0) {
        return WAREHOUSE_ROOM;
    } else if (total_loads < FULL_WAREHOUSE) {
        return WAREHOUSE_SOME_ROOM;
    } else {
        return WAREHOUSE_FULL;
    }
}

int building_warehouse_get_amount(building *warehouse, int resource)
{
    int loads = 0;
    building *space = warehouse;
    for (int i = 0; i < 8; i++) {
        space = building_next(space);
        if (space->id <= 0) {
            return 0;
        }
        if (space->subtype.warehouse_resource_id && space->subtype.warehouse_resource_id == resource) {
            loads += space->resources[resource];
        }
    }
    return loads;
}

int building_warehouse_get_available_amount(building *warehouse, int resource)
{
    if (warehouse->state != BUILDING_STATE_IN_USE || warehouse->has_plague) {
        return 0;
    }

    if (building_warehouse_is_maintaining(resource, warehouse)) {
        return 0;
    }

    return building_warehouse_get_amount(warehouse, resource);
}


int building_warehouse_add_resource(building *b, int resource, int respect_settings)
{
    if (b->id <= 0) {
        return 0;
    }
    if (respect_settings && building_warehouse_is_not_accepting(resource, building_main(b))) {
        return 0;
    }
    // Fill partially filled bays first
    int space_found = 0;
    building *space = building_main(b);
    for (int i = 0; i < 8; i++) {
        space = building_next(space);
        if (!space->id) {
            return 0;
        }
        if (space->subtype.warehouse_resource_id == resource) {
            if (space->resources[resource] < MAX_CARTLOADS_PER_SPACE) {
                space_found = 1;
                b = space;
                break;
            }
        }
    }
    // Use a new bay if there aren't any partially filled
    if (!space_found) {
        space_found = 0;
        space = building_main(b);
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (!space->id) {
                return 0;
            }
            if (!space->subtype.warehouse_resource_id || space->subtype.warehouse_resource_id == resource) {
                if (space->resources[resource] < MAX_CARTLOADS_PER_SPACE) {
                    space_found = 1;
                    b = space;
                    break;
                }
            }
        }
        if (!space_found) {
            return 0;
        }
    }
    city_resource_add_to_warehouse(resource, 1);
    b->subtype.warehouse_resource_id = resource;
    b->resources[resource]++;
    tutorial_on_add_to_warehouse();
    building_warehouse_space_set_image(b, resource);
    return 1;
}

int building_warehouses_add_resource(int resource, int amount, int respect_settings)
{
    if (amount <= 0) {
        return 0;
    }

    for (building *b = building_first_of_type(BUILDING_WAREHOUSE); b; b = b->next_of_type) {
        if (b->state != BUILDING_STATE_IN_USE) {
            continue;
        }
        int keep_adding = (amount > 0);
        while (keep_adding) {
            int was_added = building_warehouse_add_resource(b, resource, respect_settings);
            amount -= was_added;
            keep_adding = (amount > 0) && was_added;
        }
        if (amount <= 0) {
            break;
        }
    }

    return amount;
}

int building_warehouse_remove_resource(building *warehouse, int resource, int amount)
{
    // returns amount still needing removal
    if (warehouse->type != BUILDING_WAREHOUSE) {
        return amount;
    }
    return amount - building_warehouse_try_remove_resource(warehouse, resource, amount);
}

int building_warehouse_try_remove_resource(building *warehouse, int resource, int desired_amount)
{
    if (desired_amount <= 0) {
        return 0;
    }
    if (warehouse->has_plague) {
        return 0;
    }
    int remaining_desired = desired_amount;
    int removed_amount = 0;
    building *space = warehouse;
    for (int i = 0; i < 8; i++) {
        if (remaining_desired <= 0) {
            return removed_amount;
        }
        space = building_next(space);
        if (space->id <= 0) {
            continue;
        }
        if (space->subtype.warehouse_resource_id != resource || space->resources[resource] <= 0) {
            continue;
        }
        if (space->resources[resource] > remaining_desired) {
            removed_amount += remaining_desired;
            city_resource_remove_from_warehouse(resource, remaining_desired);
            space->resources[resource] -= remaining_desired;
            remaining_desired = 0;
        } else {
            removed_amount += space->resources[resource];
            city_resource_remove_from_warehouse(resource, space->resources[resource]);
            remaining_desired -= space->resources[resource];
            space->resources[resource] = 0;
            space->subtype.warehouse_resource_id = RESOURCE_NONE;
        }
        building_warehouse_space_set_image(space, resource);
    }
    return removed_amount;
}

void building_warehouse_remove_resource_curse(building *warehouse, int amount)
{
    if (warehouse->type != BUILDING_WAREHOUSE) {
        return;
    }

    building *space = warehouse;
    for (int i = 0; i < 8 && amount > 0; i++) {
        space = building_next(space);
        int resource = space->subtype.warehouse_resource_id;
        if (space->id <= 0 || space->resources[resource] <= 0) {
            continue;
        }
        if (space->resources[resource] > amount) {
            city_resource_remove_from_warehouse(resource, amount);
            space->resources[resource] -= amount;
            amount = 0;
        } else {
            city_resource_remove_from_warehouse(resource, space->resources[resource]);
            amount -= space->resources[resource];
            space->resources[resource] = 0;
            space->subtype.warehouse_resource_id = RESOURCE_NONE;
        }
        building_warehouse_space_set_image(space, resource);
    }
}

void building_warehouse_space_set_image(building *space, int resource)
{
    int image_id;
    if (building_loads_stored(space) <= 0) {
        image_id = image_group(GROUP_BUILDING_WAREHOUSE_STORAGE_EMPTY);
    } else {
        image_id = resource_get_data(resource)->image.storage + space->resources[resource] - 1;
    }
    map_image_set(space->grid_offset, image_id);
}

void building_warehouse_space_add_import(building *space, int resource, int land_trader)
{
    city_resource_add_to_warehouse(resource, 1);
    space->resources[resource]++;
    space->subtype.warehouse_resource_id = resource;

    int price = trade_price_buy(resource, land_trader);
    city_finance_process_import(price);

    building_warehouse_space_set_image(space, resource);
}

void building_warehouse_space_remove_export(building *space, int resource, int land_trader)
{
    city_resource_remove_from_warehouse(resource, 1);
    space->resources[resource]--;
    if (space->resources[resource] <= 0) {
        space->subtype.warehouse_resource_id = RESOURCE_NONE;
    }

    int price = trade_price_sell(resource, land_trader);
    city_finance_process_export(price);

    building_warehouse_space_set_image(space, resource);
}

static building *get_next_warehouse(void)
{
    int building_id = city_resource_last_used_warehouse();
    int wrapped_around = 0;
    building *b = building_first_of_type(BUILDING_WAREHOUSE);
    while (b) {
        if (b->state == BUILDING_STATE_IN_USE && (b->id > building_id || wrapped_around)) {
            return b;
        }
        if (!b->next_of_type) {
            if (wrapped_around) {
                return 0;
            }
            wrapped_around = 1;
            b = building_first_of_type(BUILDING_WAREHOUSE);
        } else {
            b = b->next_of_type;
        }
    }
    return 0;
}

int building_warehouse_is_accepting(int resource, building *b)
{
    const building_storage *s = building_storage_get(b->storage_id);
    const resource_storage_entry *entry = &s->resource_state[resource];
    int amount = building_warehouse_get_amount(b, resource);

    if (b->has_plague) {
        return 0;
    }
    if (entry->state != BUILDING_STORAGE_STATE_ACCEPTING) {
        return 0;
    }
    if (amount < entry->quantity) {
        return 1;
    }

    return 0;
}


int building_warehouse_is_getting(int resource, building *b)
{
    const building_storage *s = building_storage_get(b->storage_id);
    const resource_storage_entry *entry = &s->resource_state[resource];
    int amount = building_warehouse_get_amount(b, resource);

    if (b->has_plague) {
        return 0;
    }
    if (entry->state != BUILDING_STORAGE_STATE_GETTING) {
        return 0;
    }
    if (amount < entry->quantity) {
        return 1;
    }

    return 0;
}

int building_warehouse_is_maintaining(int resource, building *b)
{
    const building_storage *s = building_storage_get(b->storage_id);
    const resource_storage_entry *entry = &s->resource_state[resource];
    int amount = building_warehouse_get_amount(b, resource);

    if (b->has_plague) {
        return 0;
    }
    if (entry->state != BUILDING_STORAGE_STATE_MAINTAINING) {
        return 0;
    }
    if (amount <= entry->quantity) {
        return 1;
    }

    return 0;
}


static int warehouse_allows_getting(int resource, building *b)
{
    const building_storage *s = building_storage_get(b->storage_id);
    const resource_storage_entry *entry = &s->resource_state[resource];

    if (b->has_plague || (entry->state == BUILDING_STORAGE_STATE_GETTING || entry->state == BUILDING_STORAGE_STATE_MAINTAINING)) {
        return 0;
        //if the building has plague or gets or maintains resource - it doesnt allow getting
    }

    return 1;
}


int building_warehouse_is_not_accepting(int resource, building *b)
{
    return !((building_warehouse_is_accepting(resource, b)
        || building_warehouse_is_getting(resource, b) ||
        building_warehouse_is_maintaining(resource, b)));
}

static int get_acceptable_quantity(resource_type resource, building *b)
{
    const building_storage *s = building_storage_get(b->storage_id);
    const resource_storage_entry *entry = &s->resource_state[resource];

    if (entry->state == BUILDING_STORAGE_STATE_ACCEPTING ||
        entry->state == BUILDING_STORAGE_STATE_GETTING ||
        entry->state == BUILDING_STORAGE_STATE_MAINTAINING) {
        return entry->quantity;
    }

    return 0;
}


int building_warehouse_maximum_receptible_amount(resource_type resource, building *b)
{
    if (b->has_plague) {
        return 0;
    }
    int stored_amount = building_warehouse_get_amount(b, resource);
    int max_amount = get_acceptable_quantity(resource, b);
    return (max_amount > stored_amount) ? (max_amount - stored_amount) : 0;
}

int building_warehouse_max_space_for_resource(resource_type resource, building *b)
{
    int max_storable = 0;
    building *space = b;
    for (int i = 0; i < 8; i++) {
        space = building_next(space);
        if (space->id <= 0) {
            return 0;
        }
        if (space->subtype.warehouse_resource_id == resource) {
            max_storable += MAX_CARTLOADS_PER_SPACE - space->resources[resource];
        }
        if (space->subtype.warehouse_resource_id == RESOURCE_NONE) {
            max_storable += MAX_CARTLOADS_PER_SPACE;
        }
    }
    return max_storable;
}

int building_warehouses_count_available_resource(int resource, int respect_maintaining)
{
    int total = 0;
    building *b = get_next_warehouse();
    if (!b) {
        return 0;
    }
    building *initial_warehouse = b;

    do {
        if (b->state == BUILDING_STATE_IN_USE) {
            if (!respect_maintaining || !building_warehouse_is_maintaining(resource, b)) {
                total += building_warehouse_get_amount(b, resource);
            }
        }
        b = b->next_of_type ? b->next_of_type : building_first_of_type(BUILDING_WAREHOUSE);
    } while (b != initial_warehouse);

    return total;
}



int building_warehouses_send_resources_to_rome(int resource, int amount)
{
    building *b = get_next_warehouse();
    if (!b) {
        return amount;
    }
    building *initial_warehouse = b;

    // First go for non-getting warehouses
    do {
        if (b->state == BUILDING_STATE_IN_USE) {
            if (warehouse_allows_getting(resource, b)) {
                city_resource_set_last_used_warehouse(b->id);
                int remaining = building_warehouse_remove_resource(b, resource, amount);
                if (remaining < amount) {
                    int loads = amount - remaining;
                    amount = remaining;
                    map_point road;
                    if (map_has_road_access_rotation(b->subtype.orientation, b->x, b->y, 3, &road)) {
                        figure *f = figure_create(FIGURE_CART_PUSHER, road.x, road.y, DIR_4_BOTTOM);
                        f->action_state = FIGURE_ACTION_234_CARTPUSHER_GOING_TO_ROME_CREATED;
                        f->resource_id = resource;
                        f->loads_sold_or_carrying = loads;
                        f->building_id = b->id;
                    }
                }
            }
        }
        b = b->next_of_type ? b->next_of_type : building_first_of_type(BUILDING_WAREHOUSE);
    } while (b != initial_warehouse && amount > 0);

    if (amount <= 0) {
        return 0;
    }

    // If that doesn't work, take it anyway
    do {
        if ((b->state == BUILDING_STATE_IN_USE) && (!building_warehouse_is_maintaining(resource, b))) {
            city_resource_set_last_used_warehouse(b->id);
            int remaining = building_warehouse_remove_resource(b, resource, amount);
            if (remaining < amount) {
                int loads = amount - remaining;
                amount = remaining;
                map_point road;
                if (map_has_road_access_rotation(b->subtype.orientation, b->x, b->y, 3, &road)) {
                    figure *f = figure_create(FIGURE_CART_PUSHER, road.x, road.y, DIR_4_BOTTOM);
                    f->action_state = FIGURE_ACTION_234_CARTPUSHER_GOING_TO_ROME_CREATED;
                    f->resource_id = resource;
                    f->loads_sold_or_carrying = loads;
                    f->building_id = b->id;
                }
            }
        }
        b = b->next_of_type ? b->next_of_type : building_first_of_type(BUILDING_WAREHOUSE);
    } while (b != initial_warehouse && amount > 0);

    return amount;
}

int building_warehouses_remove_resource(int resource, int amount)
{
    building *b = get_next_warehouse();
    if (!b) {
        return amount;
    }
    building *initial_warehouse = b;

    // First go for non-getting, non-maintaining warehouses
    do {
        if (b->state == BUILDING_STATE_IN_USE) {
            if (!(building_warehouse_is_getting(resource, b) && building_warehouse_is_maintaining(resource, b))) {
                city_resource_set_last_used_warehouse(b->id);
                amount = building_warehouse_remove_resource(b, resource, amount);
            }
        }
        b = b->next_of_type ? b->next_of_type : building_first_of_type(BUILDING_WAREHOUSE);
    } while (b != initial_warehouse && amount > 0);

    if (amount <= 0) {
        return 0;
    }

    // If that doesn't work, take it anyway
    do {
        if (b->state == BUILDING_STATE_IN_USE) {
            city_resource_set_last_used_warehouse(b->id);
            amount = building_warehouse_remove_resource(b, resource, amount);
        }
        b = b->next_of_type ? b->next_of_type : building_first_of_type(BUILDING_WAREHOUSE);
    } while (b != initial_warehouse && amount > 0);

    return amount;
}

int building_warehouse_accepts_storage(building *b, int resource, int *understaffed)
{
    if (b->state != BUILDING_STATE_IN_USE || b->type != BUILDING_WAREHOUSE ||
        !b->has_road_access || b->distance_from_entry <= 0 || b->has_plague) {
        return 0;
    }
    const building_storage *s = building_storage_get(b->storage_id);
    if (building_warehouse_is_not_accepting(resource, b) || s->empty_all) {
        return 0;
    }
    int pct_workers = calc_percentage(b->num_workers, model_get_building(b->type)->laborers);
    if (pct_workers < 100) {
        if (understaffed) {
            *understaffed += 1;
        }
        return 0;
    }
    building *space = b;
    for (int t = 0; t < 8; t++) {
        space = building_next(space);
        if (space->subtype.warehouse_resource_id == RESOURCE_NONE || // empty warehouse space
            (space->subtype.warehouse_resource_id == resource && space->resources[resource] < MAX_CARTLOADS_PER_SPACE)) {
            return 1;
        }
    }
    return 0;
}

int building_warehouse_for_storing(int src_building_id, int x, int y, int resource, int road_network_id,
    int *understaffed, map_point *dst)
{
    int min_dist = INFINITE;
    int min_building_id = 0;
    for (building *b = building_first_of_type(BUILDING_WAREHOUSE); b; b = b->next_of_type) {
        if (b->id == src_building_id || (road_network_id != -1 && b->road_network_id != road_network_id) ||
            !building_warehouse_accepts_storage(b, resource, understaffed)) {
            continue;
        }
        int dist = calc_maximum_distance(b->x, b->y, x, y);
        if (dist < min_dist) {
            min_dist = dist;
            min_building_id = b->id;
        }
    }
    building *b = building_get(min_building_id);
    if (b->has_road_access == 1) {
        map_point_store_result(b->x, b->y, dst);
    } else if (!map_has_road_access_rotation(b->subtype.orientation, b->x, b->y, 3, dst)) {
        return 0;
    }
    return min_building_id;
}

int building_warehouse_amount_can_get_from(building *destination, int resource)
{
    int loads_stored = 0;
    building *space = destination;
    for (int t = 0; t < 8; t++) {
        space = building_next(space);
        if (space->id > 0 && space->subtype.warehouse_resource_id == resource) {
            loads_stored += space->resources[resource];
        }
    }
    return loads_stored;
}

int building_warehouse_for_getting(building *src, int resource, map_point *dst)
{
    int min_dist = INFINITE;
    building *min_building = 0;
    for (building *b = building_first_of_type(BUILDING_WAREHOUSE); b; b = b->next_of_type) {
        if (b->state != BUILDING_STATE_IN_USE || b->has_plague) {
            continue;
        }
        if (b->id == src->id) {
            continue;
        }
        int loads_stored = building_warehouse_amount_can_get_from(b, resource);
        if (loads_stored > 0 && warehouse_allows_getting(resource, b)) {
            int dist = calc_maximum_distance(b->x, b->y, src->x, src->y);
            dist -= 4 * loads_stored;
            if (dist < min_dist) {
                min_dist = dist;
                min_building = b;
            }
        }
    }
    if (min_building) {
        if (dst) {
            map_point_store_result(min_building->road_access_x, min_building->road_access_y, dst);
        }
        return min_building->id;
    } else {
        return 0;
    }
}

int building_warehouse_with_resource(int x, int y, int resource, int road_network_id, int *understaffed, map_point *dst, building_storage_permission_states p)
{
    int min_dist = INFINITE;
    building *min_building = 0;
    for (building *b = building_first_of_type(BUILDING_WAREHOUSE); b; b = b->next_of_type) {
        if (b->state != BUILDING_STATE_IN_USE || b->has_plague) {
            continue;
        }
        if (!b->has_road_access || b->distance_from_entry <= 0 || b->road_network_id != road_network_id) {
            continue;
        }
        if (!building_storage_get_permission(p, b)) {
            continue;
        }

        int pct_workers = calc_percentage(b->num_workers, model_get_building(b->type)->laborers);
        if (pct_workers < 100) {
            if (understaffed) {
                *understaffed += 1;
            }
            continue;
        }
        int loads_stored = 0;
        building *space = b;
        for (int t = 0; t < 8; t++) {
            space = building_next(space);
            if (space->id > 0 && space->subtype.warehouse_resource_id == resource) {
                loads_stored += space->resources[resource];
            }
        }
        if (loads_stored > 0) {
            int dist = calc_maximum_distance(b->x, b->y, x, y);
            dist -= 2 * loads_stored;
            if (dist < min_dist) {
                min_dist = dist;
                min_building = b;
            }
        }
    }
    if (min_building) {
        if (dst) {
            map_point_store_result(min_building->road_access_x, min_building->road_access_y, dst);
        }
        return min_building->id;
    } else {
        return 0;
    }
}

static int determine_granary_accept_foods(int resources[RESOURCE_MAX_FOOD], int road_network)
{
    if (scenario_property_rome_supplies_wheat()) {
        return 0;
    }
    for (int i = 0; i < RESOURCE_MAX_FOOD; i++) {
        resources[i] = 0;
    }
    int can_accept = 0;
    for (building *b = building_first_of_type(BUILDING_GRANARY); b; b = b->next_of_type) {
        if (b->state != BUILDING_STATE_IN_USE || !b->has_road_access || b->has_plague || road_network != b->road_network_id) {
            continue;
        }
        int pct_workers = calc_percentage(b->num_workers, model_get_building(b->type)->laborers);
        if (pct_workers >= 100 && b->resources[RESOURCE_NONE] >= 1) {
            const building_storage *s = building_storage_get(b->storage_id);
            if (!s->empty_all) {
                for (int r = 0; r < RESOURCE_MAX_FOOD; r++) {
                    if (!building_granary_is_not_accepting(r, b)) {
                        resources[r]++;
                        can_accept = 1;
                    }
                }
            }
        }
    }
    return can_accept;
}

static int determine_granary_get_foods(int resources[RESOURCE_MAX_FOOD], int road_network)
{
    if (scenario_property_rome_supplies_wheat()) {
        return 0;
    }
    for (int i = 0; i < RESOURCE_MAX_FOOD; i++) {
        resources[i] = 0;
    }
    int can_get = 0;
    for (building *b = building_first_of_type(BUILDING_GRANARY); b; b = b->next_of_type) {
        if (b->state != BUILDING_STATE_IN_USE || !b->has_road_access || b->has_plague || road_network != b->road_network_id) {
            continue;
        }
        int pct_workers = calc_percentage(b->num_workers, model_get_building(b->type)->laborers);
        if (pct_workers >= 100 && b->resources[RESOURCE_NONE] > 1) {
            const building_storage *s = building_storage_get(b->storage_id);
            if (!s->empty_all) {
                for (int r = 0; r < RESOURCE_MAX_FOOD; r++) {
                    if (building_granary_is_getting(r, b)) {
                        resources[r]++;
                        can_get = 1;
                    }
                }
            }
        }
    }
    return can_get;
}

static int contains_non_stockpiled_food(building *space, const int *resources)
{
    if (space->id <= 0) {
        return 0;
    }
    int resource = space->subtype.warehouse_resource_id;
    if (space->resources[resource] <= 0) {
        return 0;
    }
    if (city_resource_is_stockpiled(resource)) {
        return 0;
    }
    return resource_is_food(resource) && resources[resource] > 0;
}

int building_warehouse_determine_worker_task(building *warehouse, int *resource)
{
    int pct_workers = calc_percentage(warehouse->num_workers, model_get_building(warehouse->type)->laborers);
    if (pct_workers < 50) {
        return WAREHOUSE_TASK_NONE;
    }
    const building_storage *s = building_storage_get(warehouse->storage_id);
    building *space;
    // get resources
    for (int r = RESOURCE_MIN; r < RESOURCE_MAX; r++) {
        //determine if any of the resources need to be fetched becasuse of 'getting'
        if (!building_warehouse_is_getting(r, warehouse) || city_resource_is_stockpiled(r) || !resource_is_storable(r)) {
            continue;
        }
        int loads_stored = 0;
        space = warehouse;
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (space->id > 0 && space->subtype.warehouse_resource_id == r) {
                loads_stored += space->resources[r];
            }
        }
        int room = 0;
        space = warehouse;
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (space->id > 0) {
                resource_type space_resource = space->subtype.warehouse_resource_id;
                if (space_resource == RESOURCE_NONE) {
                    room += MAX_CARTLOADS_PER_SPACE;
                } else if (r == space_resource) {
                    room += MAX_CARTLOADS_PER_SPACE - space->resources[space_resource];
                }
            }
        }
        int needed = get_acceptable_quantity(r, warehouse) - loads_stored;
        int available = city_resource_count(r) - loads_stored;
        int fetch_amount = MAX_CARTLOADS_PER_SPACE;


        if (available > needed && needed >= fetch_amount && fetch_amount > 0) {
            if (!building_warehouse_for_getting(warehouse, r, 0)) {
                continue;
            }
            *resource = r;
            return WAREHOUSE_TASK_GETTING;
        }

    }
    if (!building_storage_get_permission(BUILDING_STORAGE_PERMISSION_WORKER, warehouse)) {
        return WAREHOUSE_TASK_NONE; //halt resource delivery to workshops and granaries
    }
    // deliver raw materials to workshops
    space = warehouse;
    for (int i = 0; i < 8; i++) {
        space = building_next(space);
        if (space->id > 0 && space->resources[space->subtype.warehouse_resource_id] > 0 &&
            !city_resource_is_stockpiled(space->subtype.warehouse_resource_id) &&
            building_has_workshop_for_raw_material_with_room(space->subtype.warehouse_resource_id,
                warehouse->road_network_id) &&
            !building_warehouse_is_maintaining(space->subtype.warehouse_resource_id, warehouse)) {

            *resource = space->subtype.warehouse_resource_id;
            return WAREHOUSE_TASK_DELIVERING;
        }
    }
    // deliver food to getting granary
    int granary_resources[RESOURCE_MAX_FOOD];
    if (determine_granary_get_foods(granary_resources, warehouse->road_network_id)) {
        space = warehouse;
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (contains_non_stockpiled_food(space, granary_resources)
            && !building_warehouse_is_maintaining(space->subtype.warehouse_resource_id, warehouse)) {
                *resource = space->subtype.warehouse_resource_id;
                return WAREHOUSE_TASK_DELIVERING;
            }
        }
    }
    // deliver food to accepting granary
    if (determine_granary_accept_foods(granary_resources, warehouse->road_network_id)
    && !scenario_property_rome_supplies_wheat()) {
        space = warehouse;
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (contains_non_stockpiled_food(space, granary_resources)
            && !building_warehouse_is_maintaining(space->subtype.warehouse_resource_id, warehouse)) {
                *resource = space->subtype.warehouse_resource_id;
                return WAREHOUSE_TASK_DELIVERING;
            }
        }
    }
    // move goods to other warehouses
    if (s->empty_all) {
        space = warehouse;
        for (int i = 0; i < 8; i++) {
            space = building_next(space);
            if (space->id > 0 && space->resources[space->subtype.warehouse_resource_id] > 0
            && !building_warehouse_is_maintaining(space->subtype.warehouse_resource_id, warehouse)) {
                *resource = space->subtype.warehouse_resource_id;
                return WAREHOUSE_TASK_DELIVERING;
            }
        }
    }
    return WAREHOUSE_TASK_NONE;
}
