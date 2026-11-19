// Panel Admin "Aldeanos": a real librarian villager whose offers the add-on manages itself.
// Bedrock trade tables are fixed files and the stable Script API cannot edit a villager's offers, so talking to one of
// these librarians opens the add-on's own trade menu: any guaranteed enchanted books plus vanilla-like trades.
import { world, system, ItemStack, EnchantmentTypes } from '@minecraft/server';
import { ActionFormData } from '@minecraft/server-ui';

export const TIERS = ['Novato', 'Aprendiz', 'Oficial', 'Experto', 'Maestro'];
export const TIER_XP = [0, 10, 70, 150, 250];
const TAG = 'lp_librarian';
const PROPERTY = 'lp:shop';
const RESTOCK_TICKS = 12000;

// Enchantments a librarian can sell (Bedrock ids), with their maximum level and whether they are treasure.
export const ENCHANTMENTS = {
    protection: [4], fire_protection: [4], feather_falling: [4], blast_protection: [4], projectile_protection: [4],
    thorns: [3], respiration: [3], depth_strider: [3], aqua_affinity: [1], sharpness: [5], smite: [5],
    bane_of_arthropods: [5], knockback: [2], fire_aspect: [2], looting: [3], efficiency: [5], silk_touch: [1],
    unbreaking: [3], fortune: [3], power: [5], punch: [2], flame: [1], infinity: [1], luck_of_the_sea: [3], lure: [3],
    frost_walker: [2, true], mending: [1, true], binding: [1, true], vanishing: [1, true], impaling: [5], riptide: [3],
    loyalty: [3], channeling: [1], multishot: [1], piercing: [4], quick_charge: [3], soul_speed: [3, true],
    swift_sneak: [3, true], density: [5], breach: [4], wind_burst: [3, true]
};
const RANDOM_POOL = Object.keys(ENCHANTMENTS).filter(id => !['soul_speed', 'swift_sneak', 'wind_burst'].includes(id));

/** Vanilla-style price of an enchanted book (Bedrock formula, average of its random part), capped at 64. */
export function bookPrice(id, level) {
    const [, treasure] = ENCHANTMENTS[id] ?? [1, false];
    const price = 2 + 3 * level + Math.floor((5 + 10 * level) / 2);
    return Math.min(64, treasure ? price * 2 : price);
}

// Vanilla librarian trades (non-book), per tier: [cost item, cost count, result item, result count, max uses, villager xp].
const VANILLA = [
    [['minecraft:paper', 24, 'minecraft:emerald', 1, 16, 2], ['minecraft:emerald', 9, 'minecraft:bookshelf', 1, 12, 1]],
    [['minecraft:book', 4, 'minecraft:emerald', 1, 12, 10], ['minecraft:emerald', 1, 'minecraft:lantern', 1, 12, 5]],
    [['minecraft:ink_sac', 5, 'minecraft:emerald', 1, 12, 20], ['minecraft:emerald', 1, 'minecraft:glass', 4, 12, 10]],
    [['minecraft:writable_book', 2, 'minecraft:emerald', 1, 12, 30], ['minecraft:emerald', 5, 'minecraft:clock', 1, 12, 15], ['minecraft:emerald', 4, 'minecraft:compass', 1, 12, 15]],
    [['minecraft:emerald', 20, 'minecraft:name_tag', 1, 12, 30]]
];
const BOOK_XP = [1, 5, 10, 15, 30];

/** Builds the offer list for a new librarian from the panel request. Pure: tests call it directly. */
export function buildOffers(request, random = Math.random) {
    const books = Array.isArray(request.books) ? request.books.slice(0, 12) : [];
    const offers = [];
    for (const book of books) {
        if (!ENCHANTMENTS[book.id]) throw new Error(`Encantamiento no valido: ${book.id}`);
        const [max] = ENCHANTMENTS[book.id];
        const level = Math.min(max, Math.max(1, Math.floor(Number(book.lvl) || 1)));
        const tier = Math.min(4, Math.max(0, Math.floor(Number(book.tier) || 0)));
        const price = Math.min(64, Math.max(1, Math.floor(Number(book.price) || bookPrice(book.id, level))));
        offers.push({ tier, cost: [['minecraft:emerald', price], ['minecraft:book', 1]], book: [book.id, level], uses: 0, max: 12, xp: BOOK_XP[tier] });
    }
    if (request.profile !== 'books') {
        VANILLA.forEach((trades, tier) => {
            for (const [item, count, result, amount, max, xp] of trades) offers.push({ tier, cost: [[item, count]], give: [result, amount], uses: 0, max, xp });
            const hasBook = offers.some(offer => offer.book && offer.tier === tier);
            if (request.random !== false && tier < 4 && !hasBook) {
                const id = RANDOM_POOL[Math.floor(random() * RANDOM_POOL.length)];
                const level = 1 + Math.floor(random() * ENCHANTMENTS[id][0]);
                offers.push({ tier, cost: [['minecraft:emerald', bookPrice(id, level)], ['minecraft:book', 1]], book: [id, level], uses: 0, max: 12, xp: BOOK_XP[tier] });
            }
        });
    }
    if (!offers.length) throw new Error('El bibliotecario no tendria ningun intercambio');
    offers.sort((a, b) => a.tier - b.tier);
    const start = Math.min(4, Math.max(0, Math.floor(Number(request.start) || 0)));
    return { offers, xp: TIER_XP[start], restock: 0 };
}

export function tierOf(xp) { let tier = 0; for (let i = 0; i < TIER_XP.length; ++i) if (xp >= TIER_XP[i]) tier = i; return tier; }

const ROMAN = ['I', 'II', 'III', 'IV', 'V'];
function itemName(id) { return id.replace('minecraft:', '').replace(/_/g, ' '); }
function bookName(id, level) { return `Libro: ${itemName(id)} ${ROMAN[level - 1] ?? level}`; }
function offerText(offer) {
    const cost = offer.cost.map(([item, count]) => `${count} ${itemName(item)}`).join(' + ');
    const result = offer.book ? bookName(...offer.book) : `${offer.give[1]} ${itemName(offer.give[0])}`;
    return `${cost} -> ${result}${offer.uses >= offer.max ? ' (agotado)' : ''}`;
}

function countItem(container, id) {
    let total = 0;
    for (let slot = 0; slot < container.size; ++slot) { const stack = container.getItem(slot); if (stack?.typeId === id) total += stack.amount; }
    return total;
}
function removeItem(container, id, count) {
    for (let slot = 0; slot < container.size && count > 0; ++slot) {
        const stack = container.getItem(slot);
        if (stack?.typeId !== id) continue;
        const take = Math.min(count, stack.amount);
        count -= take;
        if (take === stack.amount) container.setItem(slot, undefined);
        else { stack.amount -= take; container.setItem(slot, stack); }
    }
}

/** One purchase: checks and takes the cost, gives the result, adds villager experience. Returns a message. */
export function trade(player, shop, index) {
    const offer = shop.offers[index];
    if (!offer || offer.tier > tierOf(shop.xp)) throw new Error('Intercambio no disponible');
    if (offer.uses >= offer.max) return 'Este intercambio esta agotado; vuelve mas tarde.';
    const container = player.getComponent('minecraft:inventory')?.container;
    if (!container) throw new Error('Sin inventario');
    for (const [item, count] of offer.cost) if (countItem(container, item) < count) return `Te falta: ${count} ${itemName(item)}.`;
    let result;
    if (offer.book) {
        result = new ItemStack('minecraft:enchanted_book', 1);
        result.getComponent('minecraft:enchantable')?.addEnchantment({ type: EnchantmentTypes.get(offer.book[0]), level: offer.book[1] });
    } else result = new ItemStack(offer.give[0], offer.give[1]);
    for (const [item, count] of offer.cost) removeItem(container, item, count);
    const rest = container.addItem(result);
    if (rest) player.dimension.spawnItem(rest, player.location);
    offer.uses += 1;
    const before = tierOf(shop.xp);
    shop.xp += offer.xp;
    const after = tierOf(shop.xp);
    return after > before ? `Intercambio hecho. El bibliotecario subio a ${TIERS[after]}.` : 'Intercambio hecho.';
}

function load(villager) { try { return JSON.parse(String(villager.getDynamicProperty(PROPERTY) ?? '')); } catch { return undefined; } }
function save(villager, shop) { villager.setDynamicProperty(PROPERTY, JSON.stringify(shop)); }

function openShop(player, villager, message) {
    const shop = load(villager);
    if (!shop) return;
    // Restock twice per day, like vanilla villagers that work.
    const now = world.getAbsoluteTime();
    if (now - (shop.restock ?? 0) >= RESTOCK_TICKS) { for (const offer of shop.offers) offer.uses = 0; shop.restock = now; save(villager, shop); }
    const tier = tierOf(shop.xp);
    const visible = shop.offers.map((offer, index) => ({ offer, index })).filter(entry => entry.offer.tier <= tier);
    const form = new ActionFormData().title(`Bibliotecario - ${TIERS[tier]}`)
        .body(`${message ? message + '\n' : ''}Experiencia: ${shop.xp}${tier < 4 ? ` / ${TIER_XP[tier + 1]}` : ''}`);
    for (const { offer } of visible) form.button(offerText(offer), offer.book ? 'textures/items/book_enchanted' : undefined);
    form.show(player).then(response => {
        if (response.canceled || response.selection === undefined) return;
        const current = load(villager);
        if (!current) return;
        let result;
        try { result = trade(player, current, visible[response.selection].index); save(villager, current); }
        catch (error) { result = String(error); }
        system.run(() => openShop(player, villager, result));
    }).catch(() => { /* form closed */ });
}

/** Spawns the librarian two blocks in front of the player. */
export function spawnLibrarian(player, request) {
    const shop = buildOffers(request);
    const direction = player.getViewDirection();
    const at = player.location;
    const location = { x: at.x + direction.x * 2, y: at.y, z: at.z + direction.z * 2 };
    const villager = player.dimension.spawnEntity('minecraft:villager_v2', location);
    try { villager.triggerEvent('minecraft:spawn_librarian'); } catch { /* profession event missing in this version */ }
    villager.addTag(TAG);
    villager.nameTag = 'Bibliotecario';
    save(villager, shop);
    // Only the books the panel asked for: a random extra book may repeat one of them but is not "guaranteed".
    const guaranteed = Math.min((request.books ?? []).length, 12);
    return `Bibliotecario generado con ${guaranteed} libro(s) garantizado(s). Hablale para comerciar.`;
}

world.beforeEvents.playerInteractWithEntity.subscribe(event => {
    if (!event.target?.hasTag?.(TAG)) return;
    event.cancel = true;
    const { player, target } = event;
    system.run(() => openShop(player, target));
});
