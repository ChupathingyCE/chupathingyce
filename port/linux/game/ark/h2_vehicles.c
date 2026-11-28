/*
H2_VEHICLES.C

Halo 2's vehicles made into this engine's (a part of h2_level.c, through
h2_weapons.c, whose model, graph and tag code it uses; built alone it is
empty).

Halo 2's Warthog and Ghost are Halo 1's rigs: the same nodes at the same
places (the Warthog's hull, suspensions, engines, tires and steering wheel;
its turret's mount, chain, gun and barrels, Halo 2's separate turret object
seated where Halo 1's gun mount is; the Ghost's hull, guns, flaps and
seat). So each is made as this engine's vehicle its kit stand-in is - its
physics, seats, animation graph (suspension, steering, wheels' spin, the
gun's aim and fire), weapon, sounds and effects - with Halo 2's render
models skinned to the stand-in's own node list, in its order (the graph's
nodes are the model's by index). The Gauss Warthog is the Warthog with
Halo 2's gauss turret and its stand-in gun made the gauss cannon (Halo 2's
rate of fire, projectile speed and damage).
*/

#ifdef H2_LEVEL_INTERNAL

/* ---------- constants */

enum
{
	H2V_MAXIMUM_VEHICLES = 24,         /* (03b_newmombasa places nine kinds; Halo 3's rows beside them, h3_vehicles.c) */
	H2V_MAXIMUM_DEBRIS = 40,
	H2V_MAXIMUM_EXTRA_DEBRIS = 32,     /* (Halo 2's garbage its vehicles' damage effects throw, past h2v_debris_names) */
	/* (a second barrel: its projectile, its impact damage and its
	detonations' damage and effects - h2w_projectile_write's slots
	H2W_SLOT_DETONATION_DAMAGE..H2W_SLOT_SUPER_EFFECT) */
	H2V_BOMB_TAGS = 2 + H2W_SLOT_SUPER_EFFECT - H2W_SLOT_DETONATION_DAMAGE + 1,
	H2V_GUN_TAGS = 6,                  /* (a vehicle with a gun of its own) its gun, the gun's projectile and damage, its firing effect,
	                                      its projectile's detonation damage and detonation effect */
	H2V_MAXIMUM_PIECES = 3,
	H2V_MAXIMUM_SKELETON = 24,
};

/* ---------- structures */

/* a node of a stand-in's skeleton (in its order): its name, its parent's
place in the list, its default translation from the parent */
struct h2v_skeleton_node
{
	char const *name;
	short parent;
	float translation[3];
};

/* a Halo 2 node's name (its '_' made ' ') and the skeleton node it is */
struct h2v_rename
{
	char const *h2;
	char const *ce;
};

struct h2v_piece
{
	char const *render_model;
	float offset[3];            /* where its origin is in the vehicle */
	char const *marker;         /* (else) its origin at this marker of the first piece's (Halo 2's variant's child object's parent marker) */
};

static struct h2v_skeleton_node const h2v_warthog_skeleton[] =
{
	{ "frame hull", -1, { 0.0f, 0.0f, 0.42f } },
	{ "frame gun mount base", 0, { -0.5f, 0.0f, 0.019f } },
	{ "frame left back suspension", 0, { -0.393f, 0.1106f, -0.1899f } },
	{ "frame left front suspension", 0, { 0.4283f, 0.1106f, -0.1948f } },
	{ "frame right back suspension", 0, { -0.393f, -0.1085f, -0.1899f } },
	{ "frame right front suspension", 0, { 0.4283f, -0.1085f, -0.1948f } },
	{ "frame steering wheel", 0, { 0.1473f, 0.1696f, 0.1209f } },
	{ "frame chain", 1, { 0.1769f, 0.1348f, 0.3095f } },
	{ "frame gun", 1, { 0.2797f, 0.0f, 0.3803f } },
	{ "frame left back engine", 2, { -0.237f, 0.0894f, -0.0401f } },
	{ "frame left front engine", 3, { 0.2417f, 0.0894f, -0.0352f } },
	{ "frame right back engine", 4, { -0.237f, -0.0915f, -0.0401f } },
	{ "frame right front engine", 5, { 0.2417f, -0.0915f, -0.0352f } },
	{ "frame barrels", 8, { 0.0792f, 0.0f, 0.0961f } },
	{ "frame left back tire", 9, { 0.0f, 0.18f, 0.0f } },
	{ "frame left front tire", 10, { 0.0f, 0.18f, 0.0f } },
	{ "frame right back tire", 11, { 0.0f, -0.18f, 0.0f } },
	{ "frame right front tire", 12, { 0.0f, -0.18f, 0.0f } },
};

static struct h2v_rename const h2v_warthog_names[] =
{
	{ "hull", "frame hull" }, { "bumper", "frame hull" }, { "stationary mount", "frame hull" },
	{ "lb suspension", "frame left back suspension" }, { "lf suspension", "frame left front suspension" },
	{ "rb suspension", "frame right back suspension" }, { "rf suspension", "frame right front suspension" },
	{ "steering wheel", "frame steering wheel" },
	{ "lb engine", "frame left back engine" }, { "lf engine", "frame left front engine" },
	{ "rb engine", "frame right back engine" }, { "rf engine", "frame right front engine" },
	{ "lb tire", "frame left back tire" }, { "lf tire", "frame left front tire" },
	{ "rb tire", "frame right back tire" }, { "rf tire", "frame right front tire" },
	{ "gun mount base", "frame gun mount base" }, { "chain", "frame chain" }, { "gun", "frame gun" },
	{ "barrels", "frame barrels" }, { "cable", "frame gun" }, { "cable end", "frame gun" },
	{ "gauss rifle ammo", "frame gun" },
	{ NULL, NULL },
};

/* the anti-personnel turret on the Warthog's rig (its graph's nodes by
index: its gunner's aim turns the gun mount base - the turret's arm, its
yaw - and the gun - its pitch): Halo 2's turret's base the hull, at the
ground, the arm's pivot above it, the gun's above that (Halo 2's model's
own places); the rest unused */
static struct h2v_skeleton_node const h2v_turret_skeleton[] =
{
	{ "frame hull", -1, { 0.0f, 0.0f, 0.035f } },
	{ "frame gun mount base", 0, { 0.0f, 0.0f, 0.253f } },
	{ "frame left back suspension", 0, { -0.393f, 0.1106f, -0.1899f } },
	{ "frame left front suspension", 0, { 0.4283f, 0.1106f, -0.1948f } },
	{ "frame right back suspension", 0, { -0.393f, -0.1085f, -0.1899f } },
	{ "frame right front suspension", 0, { 0.4283f, -0.1085f, -0.1948f } },
	{ "frame steering wheel", 0, { 0.1473f, 0.1696f, 0.1209f } },
	{ "frame chain", 1, { 0.0f, 0.0f, 0.0f } },
	{ "frame gun", 1, { 0.0f, 0.0f, 0.187f } },
	{ "frame left back engine", 2, { -0.237f, 0.0894f, -0.0401f } },
	{ "frame left front engine", 3, { 0.2417f, 0.0894f, -0.0352f } },
	{ "frame right back engine", 4, { -0.237f, -0.0915f, -0.0401f } },
	{ "frame right front engine", 5, { 0.2417f, -0.0915f, -0.0352f } },
	{ "frame barrels", 8, { 0.0f, 0.0f, 0.0f } },
	{ "frame left back tire", 9, { 0.0f, 0.18f, 0.0f } },
	{ "frame left front tire", 10, { 0.0f, 0.18f, 0.0f } },
	{ "frame right back tire", 11, { 0.0f, -0.18f, 0.0f } },
	{ "frame right front tire", 12, { 0.0f, -0.18f, 0.0f } },
};

static struct h2v_rename const h2v_turret_names[] =
{
	{ "base", "frame hull" }, { "arm", "frame gun mount base" }, { "gun", "frame gun" }, { "charging handle", "frame gun" },
	{ NULL, NULL },
};

static struct h2v_skeleton_node const h2v_ghost_skeleton[] =
{
	{ "frame hull", -1, { 0.3928f, 0.0f, 0.1711f } },
	{ "frame guns", 0, { 0.3558f, -0.0006f, -0.1231f } },
	{ "frame left flap", 0, { 0.1635f, 0.3425f, -0.0765f } },
	{ "frame right flap", 0, { 0.1635f, -0.3442f, -0.0765f } },
	{ "frame seat", 0, { -0.2091f, 0.0f, -0.071f } },
};

static struct h2v_rename const h2v_ghost_names[] =
{
	{ "hull", "frame hull" }, { "guns", "frame guns" }, { "left flap", "frame left flap" },
	{ "right flap", "frame right flap" }, { "seat", "frame seat" },
	{ NULL, NULL },
};

/* the Banshee on the stand-in Ghost's rig, its nodes where Halo 2's are
(their default places, under its hull): its cab the Ghost's guns, its
rudders the Ghost's flaps (turned by its steering, as Halo 2's are), the
seat empty; each a node of its own, so its collision model's BSPs (one a
node) all stay */
static struct h2v_skeleton_node const h2v_banshee_skeleton[] =
{
	{ "frame hull", -1, { -0.02f, 0.0f, 0.425f } },
	{ "frame guns", 0, { 0.436763f, 0.0f, 0.21616f } },
	{ "frame left flap", 0, { -0.600604f, 0.238888f, 0.257062f } },
	{ "frame right flap", 0, { -0.600604f, -0.241147f, 0.257062f } },
	{ "frame seat", 0, { 0.0f, 0.0f, 0.0f } },
};

static struct h2v_rename const h2v_banshee_names[] =
{
	{ "hull", "frame hull" }, { "cab", "frame guns" }, { "left rudder", "frame left flap" }, { "right rudder", "frame right flap" },
	{ NULL, NULL },
};

/* the Wraith on the stand-in Ghost's rig (its nodes' places its model's,
h2v_model_reorder): its hull, its hatch the seat's node, its retros the
flaps' (turned by its steering), its wings and their suspensions rigid on
its hull, its mortar (a child object in Halo 2, at its "turret" marker)
on the guns' node */
static struct h2v_skeleton_node const h2v_wraith_skeleton[] =
{
	{ "frame hull", -1, { 0.0f, 0.0f, 0.0f } },
	{ "frame guns", 0, { 0.0f, 0.0f, 0.0f } },
	{ "frame left flap", 0, { 0.0f, 0.0f, 0.0f } },
	{ "frame right flap", 0, { 0.0f, 0.0f, 0.0f } },
	{ "frame seat", 0, { 0.0f, 0.0f, 0.0f } },
};

static struct h2v_rename const h2v_wraith_names[] =
{
	{ "hull", "frame hull" }, { "hatch", "frame seat" }, { "left retro", "frame left flap" }, { "right retro", "frame right flap" },
	{ "l wing suspension", "frame hull" }, { "r wing suspension", "frame hull" }, { "left back wing", "frame hull" },
	{ "right back wing", "frame hull" },
	{ "root", "frame guns" }, { "backhatch", "frame guns" }, { "gun arm", "frame guns" }, { "gun top l casing", "frame guns" },
	{ "gun top r casing", "frame guns" }, { "gun center", "frame guns" }, { "gun l siding", "frame guns" },
	{ "gun r siding", "frame guns" }, { "gun slide", "frame guns" },
	{ NULL, NULL },
};

/* the Scorpion: Halo 1's rig (the stand-in Scorpion's, the kit's from the
player's Blood Gulch): its hull, hatches and treads, and its cannon (a
child object in Halo 2, at its "turret" marker) on the turret's rotator,
turret, aiming gun and cannon - the driver's aim turns them */
static struct h2v_skeleton_node const h2v_scorpion_skeleton[] =
{
	{ "frame hull", -1, { -0.1286f, 0.0f, 0.4618f } },
	{ "frame hatch driver", 0, { 0.3809f, 0.1924f, 0.1824f } },
	{ "frame hatch passenger", 0, { 0.3809f, -0.1892f, 0.1824f } },
	{ "frame left back tread", 0, { -0.3914f, 0.56f, -0.1218f } },
	{ "frame left front tread", 0, { 0.4686f, 0.46f, -0.1218f } },
	{ "frame right back tread", 0, { -0.3914f, -0.56f, -0.1218f } },
	{ "frame right front tread", 0, { 0.4686f, -0.46f, -0.1218f } },
	{ "frame turret rotator", 0, { -0.5917f, 0.0f, 0.2896f } },
	{ "frame turret", 7, { 0.0202f, 0.0f, 0.4986f } },
	{ "frame aiming gun", 8, { 0.651f, -0.1743f, -0.0397f } },
	{ "frame cannon", 8, { 1.1197f, 0.0f, -0.0491f } },
};

static struct h2v_rename const h2v_scorpion_names[] =
{
	{ "hull", "frame hull" }, { "hatch driver", "frame hatch driver" }, { "hatch passenger", "frame hatch passenger" },
	{ "lb tread", "frame left back tread" }, { "lf tread", "frame left front tread" },
	{ "rb tread", "frame right back tread" }, { "rf tread", "frame right front tread" },
	{ "turret rotator", "frame turret rotator" }, { "turret", "frame turret" }, { "gun", "frame aiming gun" },
	{ "cannon", "frame cannon" },
	{ NULL, NULL },
};

/* the plasma cannon (Halo 2's multiplayer Covenant turret, "c_turret_mp"),
as the anti-personnel turret is, on the Warthog's rig: its foot, bipod and
toes the hull, its chamber and shell the gun - turned by the gun mount
base's yaw (at the chamber's pivot) and its own pitch */
static struct h2v_skeleton_node const h2v_plasma_cannon_skeleton[] =
{
	{ "frame hull", -1, { 0.0f, 0.0f, 0.0f } },
	{ "frame gun mount base", 0, { 0.0961f, 0.0f, 0.2547f } },
	{ "frame left back suspension", 0, { -0.393f, 0.1106f, -0.1899f } },
	{ "frame left front suspension", 0, { 0.4283f, 0.1106f, -0.1948f } },
	{ "frame right back suspension", 0, { -0.393f, -0.1085f, -0.1899f } },
	{ "frame right front suspension", 0, { 0.4283f, -0.1085f, -0.1948f } },
	{ "frame steering wheel", 0, { 0.1473f, 0.1696f, 0.1209f } },
	{ "frame chain", 1, { 0.0f, 0.0f, 0.0f } },
	{ "frame gun", 1, { 0.0f, 0.0f, 0.0f } },
	{ "frame left back engine", 2, { -0.237f, 0.0894f, -0.0401f } },
	{ "frame left front engine", 3, { 0.2417f, 0.0894f, -0.0352f } },
	{ "frame right back engine", 4, { -0.237f, -0.0915f, -0.0401f } },
	{ "frame right front engine", 5, { 0.2417f, -0.0915f, -0.0352f } },
	{ "frame barrels", 8, { 0.0f, 0.0f, 0.0f } },
	{ "frame left back tire", 9, { 0.0f, 0.18f, 0.0f } },
	{ "frame left front tire", 10, { 0.0f, 0.18f, 0.0f } },
	{ "frame right back tire", 11, { 0.0f, -0.18f, 0.0f } },
	{ "frame right front tire", 12, { 0.0f, -0.18f, 0.0f } },
};

static struct h2v_rename const h2v_plasma_cannon_names[] =
{
	{ "foot", "frame hull" }, { "bipod", "frame hull" }, { "l toe", "frame hull" }, { "r toe", "frame hull" },
	{ "chamber", "frame gun" }, { "shell", "frame gun" },
	{ NULL, NULL },
};

/* a stand-in's seat as Halo 2's vehicle's (its label - the riders' graphs
have Halo 2's seats' modes by it, h2_animation.c -, its marker, NULL the
stand-in's) */
struct h2v_seat_name
{
	char const *ce;
	char const *h2;
	char const *marker;
};

/* the Spectre on the stand-in Warthog's rig (its nodes' places its
model's): its hull and the rest rigid on it, its plasma turret (a child
object at its "turret" marker in Halo 2) the Warthog's gun mount base -
its yaw - and gun - its pitch -, which its gunner's aim turns */
static struct h2v_rename const h2v_spectre_names[] =
{
	{ "hull", "frame hull" }, { "cockpit dash", "frame hull" }, { "cockpit hatch", "frame hull" }, { "cockpit shield", "frame hull" },
	{ "left antigrav", "frame hull" }, { "right antigrav", "frame hull" }, { "turret arm", "frame hull" },
	{ "stationary mount", "frame gun mount base" }, { "root", "frame gun mount base" }, { "turret base", "frame gun mount base" },
	{ "turret gun", "frame gun" }, { "turret slide", "frame gun" },
	{ NULL, NULL },
};

static struct h2v_seat_name const h2v_banshee_seats[] = { { "G-driver", "banshee_d", NULL }, { NULL, NULL, NULL } };
/* (the campaign's: the Pelican - its pilot the stand-in's seat, its troops'
seats Halo 2's own, h2v_seats_write - and the Prophets' gravity thrones) */
static struct h2v_seat_name const h2v_pelican_seats[] = { { "G-driver", "pelican_d", NULL }, { NULL, NULL, NULL } };
/* (the campaign's Phantom: its pilot the stand-in's seat; its troops' and
cargo seats Halo 2's own, h2v_seats_write's own_seats) */
static struct h2v_seat_name const h2v_phantom_seats[] = { { "G-driver", "phantom_d", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_throne_seats[] = { { "G-driver", "throne_d", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_wraith_seats[] = { { "G-driver", "wraith_d", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_plasma_cannon_seats[] = { { "W-gunner", "c_turret_mp_d", "gunner" }, { NULL, NULL, NULL } };
/* (the campaign's mounted machine gun - Outskirts' -, objects\weapons\fixed\h_turret_mp) */
static struct h2v_seat_name const h2v_machine_gun_seats[] = { { "W-gunner", "h_turret_mp_d", "gunner" }, { NULL, NULL, NULL } };
/* (the multiplayer human turret, objects\vehicles\h_turret_ap: its gunner
Halo 2's h_turret_ap_d seat - its own 13 animations in the Spartan's graph,
not the Warthog gunner's -, at its "gunner" marker) */
static struct h2v_seat_name const h2v_ap_turret_seats[] = { { "W-gunner", "h_turret_ap_d", "gunner" }, { NULL, NULL, NULL } };
/* (the campaign's Covenant turret, objects\vehicles\c_turret_ap, and the
Covenant gun tower's mortar turret) */
static struct h2v_seat_name const h2v_covenant_turret_seats[] = { { "W-gunner", "c_turret_ap_d", "gunner" }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_guntower_seats[] = { { "W-gunner", "guntower_d", "driver" }, { NULL, NULL, NULL } };
/* (the Shadow, objects\vehicles\creep: its driver the stand-in Ghost's,
its gunner and twelve riders copies of it) */
/* (the Covenant insertion pod an Elite rides down in: its one seat) */
static struct h2v_seat_name const h2v_insertion_pod_seats[] = { { "G-driver", "insertion_pod_d", "rider" }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_creep_seats[] = { { "G-driver", "creep_d", "creep d" }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h2v_creep_extra_seats[] =
{
	{ "G-driver", "creep_g", "creep g" },
	{ "G-driver", "creep_p_l01", "creep p l01" }, { "G-driver", "creep_p_l02", "creep p l02" }, { "G-driver", "creep_p_l03", "creep p l03" },
	{ "G-driver", "creep_p_l04", "creep p l04" }, { "G-driver", "creep_p_l05", "creep p l05" },
	{ "G-driver", "creep_p_r01", "creep p r01" }, { "G-driver", "creep_p_r02", "creep p r02" }, { "G-driver", "creep_p_r03", "creep p r03" },
	{ "G-driver", "creep_p_r04", "creep p r04" }, { "G-driver", "creep_p_r05", "creep p r05" },
	{ "G-driver", "creep_sc01", "creep sc01" },
	{ NULL, NULL, NULL },
};

static struct h2v_seat_name const h2v_scorpion_seats[] =
{
	{ "scorpion-driver", "scorpion_d", NULL },
	{ "scorpionLF", "scorpion_p_lf", "passenger lf" }, { "scorpionRF", "scorpion_p_rf", "passenger rf" },
	{ "scorpionLB", "scorpion_p_lb", "passenger lb" }, { "scorpionRB", "scorpion_p_rb", "passenger rb" },
	{ NULL, NULL, NULL },
};

static struct h2v_seat_name const h2v_spectre_seats[] =
{
	{ "W-driver", "spectre_d", "spectre d" }, { "W-passenger", "spectre_p_l", "spectre p l" }, { "W-gunner", "spectre_g", "spectre g" },
	{ NULL, NULL, NULL },
};

/* (h3_vehicles.c: Halo 3's vehicles of no Halo 2 twin - the Mongoose on the
Warthog's stand-in, the Hornet and the Chopper on the Ghost's) */
static struct h2v_seat_name const h3v_mongoose_seats[] = { { "W-driver", "mongoose_d", NULL }, { "W-passenger", "mongoose_p", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h3v_hornet_seats[] = { { "G-driver", "hornet_d", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h3v_chopper_seats[] = { { "G-driver", "chopper_d", NULL }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h3v_turret_seats[] = { { "W-gunner", "turret_g", "turret g" }, { NULL, NULL, NULL } };
static struct h2v_seat_name const h3v_shade_seats[] = { { "W-gunner", "shade_d", "shade d" }, { NULL, NULL, NULL } };

/* the vehicles converted: the netgame collection's name, the kit's
vehicle it is made from, its skeleton and Halo 2's names for it, its
pieces, and (a Gauss Warthog's) Halo 2's weapon its gun becomes */
static struct h2v_kind
{
	char const *collection;
	char const *base;
	struct h2v_skeleton_node const *skeleton;
	long skeleton_count;
	struct h2v_rename const *names;
	struct h2v_piece pieces[H2V_MAXIMUM_PIECES];
	char const *gun;
	boolean gun_bolt;           /* (the gauss cannon) its projectile the kit's rocket made a bolt; else the stand-in gun's own, made Halo 2's */
	boolean unturned;           /* (the turret) its nodes' default turns made none (h2v_model_unturn) */
	boolean own_collision;      /* (the turret) a collision model and seat camera of Halo 2's own (two more tags: h2v_tags) */
	boolean own_body;           /* (a vehicle not of its stand-in's shape) its stand-in's physics with Halo 2's centre of mass (one more tag: h2v_tags) */
	boolean gun_arc;            /* (gun_bolt) its bolt falls by Halo 2's projectile's gravity scale (the Wraith's mortar) */
	boolean places_from_model;  /* its nodes' places its model's (h2v_model_reorder from_model; own_body's always) */
	boolean placed_only;        /* made only on a level that places one (its netgame items' collections): its models and the kit's stand-in otherwise not loaded */
	struct h2v_seat_name const *seat_names; /* (NULL: the Warthog's and Ghost's) its stand-in's seats as Halo 2's */
	boolean own_graph;          /* its own rig and graph: Halo 2's nodes as they are (no stand-in rig), its animations Halo 2's (h2v_graph_make; one more tag) */
	struct h2v_seat_name const *extra_seats;   /* (NULL none) seats past its stand-in's: each a copy of the stand-in's seat
	                                              named ce (a gunner's - "_g" - its weapon's, the rest riders), Halo 2's label and marker */
	boolean h3;                 /* (h3_vehicles.c) a Halo 3 vehicle's row: pieces[0] its vehi's path in the Halo 3 map, never made from a Halo 2 map */
} const h2v_kinds[] =
{
	{
		"warthog_gauss", "vehicles\\warthog\\warthog", h2v_warthog_skeleton, 18, h2v_warthog_names,
		{
			{ "objects\\vehicles\\warthog\\warthog", { 0.0f, 0.0f, 0.0f } },
			{ "objects\\vehicles\\warthog\\turrets\\gauss\\gauss", { -0.5f, 0.0f, 0.439f } },
		},
		"objects\\vehicles\\warthog\\turrets\\gauss\\weapon\\gauss_turret", TRUE,
	},
	{
		"warthog", "vehicles\\warthog\\warthog", h2v_warthog_skeleton, 18, h2v_warthog_names,
		{
			{ "objects\\vehicles\\warthog\\warthog", { 0.0f, 0.0f, 0.0f } },
			{ "objects\\vehicles\\warthog\\turrets\\chaingun\\chaingun", { -0.5f, 0.0f, 0.439f } },
		},
		"objects\\vehicles\\warthog\\turrets\\chaingun\\weapon\\chaingun_turret", FALSE,
	},
	{
		"h_turret_ap", "vehicles\\warthog\\warthog", h2v_turret_skeleton, 18, h2v_turret_names,
		{
			{ "objects\\vehicles\\h_turret_ap\\h_turret_ap", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\h_turret_ap\\weapon\\h_turret_ap", FALSE, TRUE, TRUE, FALSE, FALSE, FALSE, FALSE,
		h2v_ap_turret_seats, TRUE,
	},
	{
		"ghost", "vehicles\\ghost\\ghost_mp", h2v_ghost_skeleton, 5, h2v_ghost_names,
		{
			{ "objects\\vehicles\\ghost\\ghost", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\ghost\\weapon\\ghost_gun", FALSE,
	},
	{
		"banshee", "vehicles\\ghost\\ghost_mp", h2v_banshee_skeleton, 5, h2v_banshee_names,
		{
			{ "objects\\vehicles\\banshee\\banshee", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\banshee\\weapon\\banshee_gun", FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_banshee_seats, TRUE,
	},
	/* (the campaign's, flown on the Banshee's stand-in: Halo 2's own rig and
	graph - the cutscenes pose them, h2_cinematics.c) */
	{
		"pelican", "vehicles\\ghost\\ghost_mp", h2v_banshee_skeleton, 5, h2v_banshee_names,
		{
			{ "objects\\vehicles\\pelican\\pelican", { 0.0f, 0.0f, 0.0f } },
		},
		NULL, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_pelican_seats, TRUE,
	},
	{
		"phantom", "vehicles\\ghost\\ghost_mp", h2v_banshee_skeleton, 5, h2v_banshee_names,
		{
			{ "objects\\vehicles\\phantom\\phantom", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\phantom\\plasma_turret\\weapons\\phantom_turret", FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_phantom_seats, TRUE,
	},
	{
		"gravity_throne", "vehicles\\ghost\\ghost_mp", h2v_banshee_skeleton, 5, h2v_banshee_names,
		{
			{ "objects\\vehicles\\gravity_throne\\gravity_throne", { 0.0f, 0.0f, 0.0f } },
		},
		NULL, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_throne_seats, TRUE,
	},
	{
		"wraith", "vehicles\\ghost\\ghost_mp", h2v_wraith_skeleton, 5, h2v_wraith_names,
		{
			{ "objects\\vehicles\\wraith\\wraith", { 0.0f, 0.0f, 0.0f } },
			{ "objects\\vehicles\\wraith\\turrets\\mortar\\mortar", { 0.0f, 0.0f, 0.0f }, "turret" },
		},
		"objects\\vehicles\\wraith\\turrets\\mortar\\mortar_turret_mp", TRUE, TRUE, TRUE, TRUE, TRUE, FALSE, TRUE,
		h2v_wraith_seats, TRUE,
	},
	{
		"scorpion", "vehicles\\scorpion\\scorpion_mp", h2v_scorpion_skeleton, 11, h2v_scorpion_names,
		{
			{ "objects\\vehicles\\scorpion\\scorpion", { 0.0f, 0.0f, 0.0f } },
			{ "objects\\vehicles\\scorpion\\turrets\\cannon\\cannon", { 0.0f, 0.0f, 0.0f }, "turret" },
		},
		"objects\\vehicles\\scorpion\\turrets\\cannon\\weapon\\cannon_turret_mp", FALSE, FALSE, FALSE, FALSE, FALSE, TRUE, TRUE,
		h2v_scorpion_seats,
	},
	{
		"spectre", "vehicles\\warthog\\warthog", h2v_warthog_skeleton, 18, h2v_spectre_names,
		{
			{ "objects\\vehicles\\spectre\\spectre", { 0.0f, 0.0f, 0.0f } },
			{ "objects\\vehicles\\spectre\\turrets\\plasma\\plasma", { 0.0f, 0.0f, 0.0f }, "turret" },
		},
		"objects\\vehicles\\spectre\\plasma_turret\\weapon\\plasma_turret", FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_spectre_seats, TRUE,
	},
	{
		"c_turret_mp", "vehicles\\warthog\\warthog", h2v_plasma_cannon_skeleton, 18, h2v_plasma_cannon_names,
		{
			{ "objects\\weapons\\fixed\\plasma_cannon\\plasma_cannon", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\weapons\\fixed\\plasma_cannon\\plasma_cannon", FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE,
		h2v_plasma_cannon_seats, TRUE,
	},
	{
		/* (the mounted machine gun the campaign places - Outskirts' plaza
		and courtyards -: the plasma cannon's kind, Halo 2's own rig,
		collision and seat camera) */
		"h_turret_mp", "vehicles\\warthog\\warthog", h2v_plasma_cannon_skeleton, 18, h2v_plasma_cannon_names,
		{
			{ "objects\\weapons\\fixed\\h_turret_mp\\h_turret_mp", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\weapons\\fixed\\h_turret_mp\\h_turret_mp", FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE,
		h2v_machine_gun_seats, TRUE,
	},
	{
		/* (the campaign's Covenant anti-personnel turret - its big needler -
		that squads start in: Outskirts, Gas Mine, Delta Halo...) */
		"c_turret_ap", "vehicles\\warthog\\warthog", h2v_plasma_cannon_skeleton, 18, h2v_plasma_cannon_names,
		{
			{ "objects\\vehicles\\c_turret_ap\\c_turret_ap", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\c_turret_ap\\weapon\\big_needler", FALSE, TRUE, TRUE, FALSE, FALSE, TRUE, TRUE,
		h2v_covenant_turret_seats, TRUE,
	},
	{
		/* (Outskirts' two Covenant gun towers' mortar turrets: a fixed
		turret, its mortar the Wraith's way - the kit rocket's bolt,
		falling) */
		"cov_guntower_turret", "vehicles\\warthog\\warthog", h2v_plasma_cannon_skeleton, 18, h2v_plasma_cannon_names,
		{
			{ "scenarios\\objects\\covenant\\military\\cov_guntower\\cov_guntower_turret\\cov_guntower_turret", { 0.0f, 0.0f, 0.0f } },
		},
		"scenarios\\objects\\covenant\\military\\cov_guntower\\cov_guntower_turret\\weapon\\cov_guntower_mortar", TRUE, TRUE, TRUE, FALSE, TRUE, TRUE, TRUE,
		h2v_guntower_seats, TRUE,
	},
	{
		/* (the Shadow: Outskirts' Covenant troop transport, nine squads -
		a hovering truck on the stand-in Ghost's body as the Wraith is,
		Halo 2's rig, collision, plasma turret, its driver, gunner and
		riders' seats) */
		"creep", "vehicles\\ghost\\ghost_mp", h2v_wraith_skeleton, 5, h2v_wraith_names,
		{
			{ "objects\\vehicles\\creep\\creep", { 0.0f, 0.0f, 0.0f } },
		},
		"objects\\vehicles\\creep\\plasma_turret\\weapons\\creep_turret", FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_creep_seats, TRUE, h2v_creep_extra_seats,
	},
	{
		/* (the Covenant insertion pod - 03a's, 06a's, 08a's Elites drop in
		one, the level's machine lowering it - made as the gravity throne
		is: no gun, its seat insertion_pod_d) */
		"insertion_pod", "vehicles\\ghost\\ghost_mp", h2v_banshee_skeleton, 5, h2v_banshee_names,
		{
			{ "objects\\vehicles\\insertion_pod\\insertion_pod", { 0.0f, 0.0f, 0.0f } },
		},
		NULL, FALSE, TRUE, TRUE, TRUE, FALSE, FALSE, TRUE,
		h2v_insertion_pod_seats, TRUE,
	},	/* ---- Halo 3's (h3_vehicles.c, HALO_H3_VEHICLES=1): pieces[0] the vehi's
	path in the Halo 3 map (its hlmt's mode, its default variant's child
	objects at their markers), Halo 3's own rig and graph, its physics
	model's body; the gun its child's or its own unit weapon (read there,
	not by name here); flags as the Halo 2 kind of the same stand-in */
	{
		"warthog", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\vehicles\\warthog\\warthog", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, NULL, TRUE, NULL, TRUE,
	},
	{
		"warthog_gauss", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\vehicles\\warthog\\warthog_gauss", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, NULL, TRUE, NULL, TRUE,
	},
	{
		"mongoose", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\vehicles\\mongoose\\mongoose", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_mongoose_seats, TRUE, NULL, TRUE,
	},
	{
		"ghost", "vehicles\\ghost\\ghost_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\ghost\\ghost", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, NULL, TRUE, NULL, TRUE,
	},
	{
		"banshee", "vehicles\\ghost\\ghost_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\banshee\\banshee", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h2v_banshee_seats, TRUE, NULL, TRUE,
	},
	{
		"hornet", "vehicles\\ghost\\ghost_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\hornet\\hornet", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_hornet_seats, TRUE, NULL, TRUE,
	},
	{
		"brute_chopper", "vehicles\\ghost\\ghost_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\brute_chopper\\brute_chopper", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_chopper_seats, TRUE, NULL, TRUE,
	},
	{
		"wraith", "vehicles\\ghost\\ghost_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\wraith\\wraith", { 0.0f, 0.0f, 0.0f } } },
		NULL, TRUE, FALSE, FALSE, TRUE, TRUE, FALSE, TRUE, h2v_wraith_seats, TRUE, NULL, TRUE,
	},
	{
		"scorpion", "vehicles\\scorpion\\scorpion_mp", NULL, 0, NULL,
		{ { "objects\\vehicles\\scorpion\\scorpion", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h2v_scorpion_seats, TRUE, NULL, TRUE,
	},	/* (Halo 3's mounted turrets: vehicles of the turret physics type, one
	gunner's seat, an integrated unit weapon - the machine gun turret and the
	plasma cannon the multiplayer maps place, the Shade - on the stand-in
	Warthog as Halo 2's mounted guns are; HALO_H3_TURRETS=0: not made) */
	{
		"machinegun_turret", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\weapons\\turret\\machinegun_turret\\machinegun_turret", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_turret_seats, TRUE, NULL, TRUE,
	},
	{
		"plasma_cannon", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\weapons\\turret\\plasma_cannon\\plasma_cannon", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_turret_seats, TRUE, NULL, TRUE,
	},
	{
		"shade", "vehicles\\warthog\\warthog", NULL, 0, NULL,
		{ { "objects\\vehicles\\shade\\shade", { 0.0f, 0.0f, 0.0f } } },
		NULL, FALSE, FALSE, FALSE, TRUE, FALSE, FALSE, TRUE, h3v_shade_seats, TRUE, NULL, TRUE,
	},
};

struct h2v_vehicle
{
	long kind;
	char name[64];
	struct h2w_model model;
	long base_vehicle;          /* the kit's */
	long tag;                   /* its tags' first (from the vehicles' first), 1 or 1 + H2V_GUN_TAGS of them */
	/* (a Gauss Warthog's) Halo 2's gun's fields */
	float gun_rounds_per_second;
	float gun_illumination_recovery;   /* (its barrel's @A0: seconds its light fades over after a round) */
	float gun_rounds_per_second_maximum;
	float gun_rate_times[2];    /* (its barrel's spin up and down, seconds) */
	float gun_error[2];         /* (its barrel's error angles, radians) */
	float gun_velocity;
	float gun_gravity;          /* (its projectile's air gravity scale, 0x164) */
	/* (its second barrel's - the Banshee's fuel rod bomb, on the melee
	button: its shots a second - one each fire recovery time -, its
	projectile's speed, gravity scale and guided turn rate) */
	float gun2_rate, gun2_velocity, gun2_gravity, gun2_guided;
	long gun2_projectile;       /* (Halo 2's: its damage and effects, h2w_projectile_write) */
	float gun_damage[3];
	float gun_radius[2];
	long gun_damage_tag;        /* Halo 2's gun's damage effect (NONE: none): its damage groups (h2_damage.c) */
	long gun_detonation_tag;    /* its projectile's detonation damage effect, Halo 2's (NONE: none) */
	boolean gun_impact;         /* its projectile has an impact damage of Halo 2's (gun_damage_tag's) */
	long gun2_impact_tag;       /* (the Scorpion: its stand-in's second trigger its machine gun) Halo 2's second barrel's bullet's damage */
	struct h2vd_definition *damage;     /* its damage sections, Halo 2's (h2v_damage_read; NULL none) */
	/* (own body) Halo 2's physics model's convex shapes (its polyhedra's
	boxes: centre, half extents), made its hull's spheres against the world */
	long shape_count;
	float shapes[12][6];
	long damage_effects[H2VD_DEFINITION_NAMES];     /* (its names' effects: Halo 2's tags, NONE a debris name) */
	short gun_sound;            /* (h2w.sounds': its firing effect's sound, Halo 2's; NONE none) */
	/* Halo 2's driving (h2v_vehicle_make) */
	float driving[9];
	boolean has_driving;
	float vitality;             /* (Halo 2's model's damage info's maximum vitality) */
	float body_regeneration[4]; /* (its damage info's) minimum stun damage, stun time, recharge time, recharge fraction */
	float shield[4];            /* (its damage info's) shield vitality, minimum stun damage, stun time, recharge time (none: 0) */
	/* (its damage info's damage seats: a seat's label, its direct scale,
	fall-off radius, maximum and minimum transfer scales; h2_damage.c) */
	long damage_seat_count;
	/* (its seats' labels and maximum relative velocities: boarding, H2
	unit_object_type.cpp's seat test - a boarder no faster or slower than
	the vehicle by more than it) */
	long seat_count;
	struct h2v_seat
	{
		char label[32];
		char marker[32];        /* (its marker name, 8) */
		float maximum_relative_velocity;
		float entry_radius;         /* (0x98) how near a unit is to enter it (Halo 2's 1.25..2; this engine's 1) */
		unsigned long flags;
		char camera_marker[32];     /* (0x60, as h2w_name writes it) its camera's marker */
		float camera_pitch[2];      /* (0x6C, 0x70) its camera's pitch range */
		/* (0x74, its first camera track's control points - a position and an
		orientation each -, h2v_seat_track_write; track_slot its tag's place
		among the vehicle's seat tracks, h2v_seat_track_tag; NONE none) */
		long camera_point_count;
		float camera_points[16][7];
		long track_slot;
	} seats[16];
	/* (its child objects' seats - the Warthog's gunner on its turret -: for
	their cameras alone, h2v_seat_track_tag; their slots after the seats') */
	long child_seat_count;
	struct h2v_seat child_seats[4];
	long seat_track_count;
	/* (its sliding: maximum left and right slide, acceleration and
	deceleration, a Ghost's strafe) */
	float slide[4];
	/* (its flip: the minimum and maximum flipping angular velocity, a
	second's) */
	float flip[2];
	/* (splatter: its collision damage transfer scale, obje +0x6C) */
	float collision_transfer_scale;
	/* (its acceleration scale, obje +0x14: how far a damage's or an
	effect's push moves it - damage.cpp function_d9640, the effects'
	function_179fb0) */
	float acceleration_scale;
	struct
	{
		char label[32];
		float scales[4];
		boolean child;          /* (a seat of one of its child objects' - the Warthog's gunner's, its turret's) */
	} damage_seats[12];
	struct h2_lock_port lock;   /* (its model's targets locked by tracking: h2_projectiles.h; tag 0 none) */
	short blip_size;            /* (its motion sensor blip's size, 0x194: 0 medium, 1 small, 2 large; -1 none read) */
	/* Halo 2's force model's values (h2_vehicle_physics.c; its tag after
	its gun's, "<name> physics") */
	struct h2vp_definition physics;
	/* (own_collision: Halo 2's collision model and render model tags - its
	"<name> collision" -, its gunner's seat's camera: marker, pitch range
	and its first track's control points - its "<name> camera") */
	long h2_collision, h2_render_model;
	/* (own graph) each node's Halo 2 frame before it was unturned: its
	default turn in the model (W, rows: local to model), its parent's (Wp),
	its default translation from its Halo 2 parent, its translation here
	(from its parent here); the turn convention of Halo 2's quaternions
	(h2v_native_convention: 0 as written, 1 transposed); its graph */
	struct h2v_native_node
	{
		float W[9], Wp[9], Dt[3], t[3];
	} native[H2W_MAXIMUM_NODES];
	int convention;
	struct h2w_graph *graph;
	/* (its animations' blend screens: Halo 2's aiming screen bounds - right
	and left yaw a frame, their frame counts, down and up pitch a frame,
	theirs -, has_screen 0 none) */
	unsigned char screens[H2W_MAXIMUM_ANIMATIONS][0x18];
	boolean has_screen[H2W_MAXIMUM_ANIMATIONS];
	/* (its hatch or canopy overlays: each piece's "combat:opening" made an
	overlay, h2v_hatch_make; hatch_count 0 none) */
	short hatches[2];
	long hatch_count;   /* (their function: H2V_HATCH_FUNCTION) */
	char camera_marker[32];
	float camera_pitch[2];
	long camera_point_count;
	float camera_points[16][7];
};

/* the pieces a vehicle's damage throws off (Halo 2's garbage, h2v_debris_names):
this engine's garbage, which goes after 10 to 20 seconds; the port's vehicle
damage (h2_vehicle_wrecks.c) throws them, by name, h2\\vehicles\\debris\\<name> */
struct h2v_debris
{
	char name[32];
	struct h2w_model model;
};

static char const *const h2v_debris_names[][2] =
{
	{ "tire", "objects\\vehicles\\warthog\\garbage\\tire\\tire" },
	{ "hubcap", "objects\\vehicles\\warthog\\garbage\\hubcap\\hubcap" },
	{ "lf fender", "objects\\vehicles\\warthog\\garbage\\lf_fender\\lf_fender" },
	{ "rf fender", "objects\\vehicles\\warthog\\garbage\\rf_fender\\rf_fender" },
	{ "lb fender", "objects\\vehicles\\warthog\\garbage\\lb_fender\\lb_fender" },
	{ "rb fender", "objects\\vehicles\\warthog\\garbage\\rb_fender\\rb_fender" },
	{ "bumper", "objects\\vehicles\\warthog\\garbage\\bumper\\bumper" },
	{ "sailpanel", "objects\\vehicles\\warthog\\garbage\\sailpanel\\sailpanel" },
	{ "winch", "objects\\vehicles\\warthog\\garbage\\winch\\winch" },
	{ "seat", "objects\\vehicles\\ghost\\garbage\\seat\\seat" },
	{ "hull shell", "objects\\vehicles\\ghost\\garbage\\hull_shell\\hull_shell" },
	{ "l wing shell", "objects\\vehicles\\ghost\\garbage\\l_wing_shell\\l_wing_shell" },
	{ "r wing shell", "objects\\vehicles\\ghost\\garbage\\r_wing_shell\\r_wing_shell" },
};

/* (a vehicle's model) Halo 2's regions made one: a Warthog's sides (this
engine's objects have 8 regions, its has 9) */
/* (Halo 2's regions merged into one of this engine's - an object has eight
at most: the Scorpion has eleven, the Banshee nine -, each pair one damage
section sets together: a Scorpion tread's armour and the tread, the
Banshee's lights and its cab, so that each keeps its damage states) */
static char const *const h2v_region_merge[] =
{
	"l hull", "sides", "r hull", "sides",
	"lf armor", "lf tread", "rf armor", "rf tread", "lb armor", "lb tread", "rb armor", "rb tread",
	"lights", "cab",
	NULL, NULL
};

static struct
{
	long count;
	struct h2v_vehicle *vehicles[H2V_MAXIMUM_VEHICLES];
	long kit_rocket;            /* (the gauss cannon's bolt's stand-in: its detonation) */
	long kit_bolt;              /* (its look: the plasma rifle's charged bolt's attachments - its glow, its light) */
	long kit_smoke, kit_fire_explosion;   /* (a vehicle's damage smoke, its death: the frag grenade's) */
	long kit_item;              /* (the debris' item fields: the assault rifle's - its bounce sound, its material effects) */
	long kit_damage;            /* (a gun whose projectile has no impact damage of its own - the Scorpion's shell -: the assault rifle bullet's, made Halo 2's) */
	long debris_count;
	struct h2v_debris *debris[H2V_MAXIMUM_DEBRIS];
	/* (Halo 2's garbage the vehicles' damage effects throw, not in
	h2v_debris_names: its name and its render model's tag name) */
	long extra_debris_count;
	char extra_debris[H2V_MAXIMUM_EXTRA_DEBRIS][2][96];
	/* (Halo 2's boarders' blow on a hatch - the Wraith's, the Scorpion's:
	the Spartan's boarding melee damage, NONE none; its tag the last,
	"h2\\vehicles\\boarding melee": h2_vehicle_damage.c) */
	long board_melee;
	/* Halo 2's damage effects the vehicles' damage responses deal (their
	transition damage effects - a death's blast -), made jpt!s:
	h2\\vehicles\\damage\\<its name> */
	long blast_count;
	struct h2_cache *cache_for_names;   /* (h2_vehicles_tag_name's: the blasts' names) */
	long blasts[16];
	/* Halo 2's damage effects the campaign level's scripts name (damage_new,
	damage_players: 01b's boarding craft shakes, shockwaves, carrier flyby,
	glass breaker), made jpt!s before the boarding melee: h2\\<its Halo 2
	name>, found by it in h2_scripts.c */
	long script_damage_count;
	long script_damages[32];
	long kit_blast;             /* (their base: the kit's frag grenade's explosion - an area damage) */
	float splatter[4];          /* (the players' collision damage limits: the Spartan's obje +0x70..0x7C) */
} h2v;

/* (h3_vehicles.c, which h2_level.c compiles after h3_weapons.c: Halo 3's
vehicles on the rows from the first h3 one) */
static boolean h3_vehicles_replaces(long kind);
static void h3_vehicles_gather(struct build *build);
H2_ALIGN static long h2v_h3_kind_first(void)
{
	long k;

	for (k = 0; k < (long)(sizeof(h2v_kinds) / sizeof(h2v_kinds[0])) && !h2v_kinds[k].h3; k++)
		;
	return k;
}
#define H2V_H3_KIND_FIRST h2v_h3_kind_first()

/* ---------- private code */

static long h2v_tags(struct h2v_vehicle const *v);

H2_ALIGN static long h2v_tag_named(struct h2_cache *cache, unsigned long group, char const *name)
{
	long i;

	/* (no Halo 2 map: a Halo 3 level's sandbox, h3_sandbox.c) */
	if (!cache)
		return NONE;
	for (i = 0; i < h2_cache_tag_count(cache); i++)
	{
		struct h2_cache_tag c;

		if (h2_cache_tag_get(cache, i, &c) && c.group_tag == group && !strcmp(h2_cache_tag_name(cache, i), name))
			return i;
	}
	return NONE;
}

/* the model tag (hlmt) of a piece's render model when it has no hlmt of its
name (the Spectre's plasma gun: render model "plasma", its model
"plasma_turret"): the first hlmt whose render model it is, preferring one
with a collision model */
H2_ALIGN static long h2v_piece_hlmt(struct h2_cache *cache, char const *render_model)
{
	long exact = h2v_tag_named(cache, 'hlmt', render_model), render = h2v_tag_named(cache, 'mode', render_model), i, found = NONE;
	struct h2_cache_tag c;

	if (exact != NONE)
		return exact;
	for (i = 0; render != NONE && i < h2_cache_tag_count(cache); i++)
	{
		if (h2_cache_tag_get(cache, i, &c) && c.group_tag == 'hlmt' && h2_cache_reference(cache, c.address + 0x0) == render)
		{
			if (h2_cache_reference(cache, c.address + 0x8) != NONE)
				return i;
			if (found == NONE)
				found = i;
		}
	}
	return found;
}

/* the model's nodes put in the skeleton's order (those it lacks made, with
the skeleton's defaults and no vertices), its vertices' nodes and markers
moved with them; FALSE if a node of the model is not the skeleton's.
from_model (an unturned model's): a node the model has placed where its
default is (its inverse's place, less its parent's), not at the
skeleton's translation - a vehicle on a rig not its own (the Wraith's
mortar at its hull's "turret" marker) */
H2_ALIGN static boolean h2v_model_reorder(struct h2w_model *m, struct h2v_skeleton_node const *skeleton, long count, boolean from_model)
{
	struct h2w_node nodes[H2W_MAXIMUM_NODES];
	short map[H2W_MAXIMUM_NODES];
	float world[H2V_MAXIMUM_SKELETON][3];
	long i, k, p;

	if (count > H2V_MAXIMUM_SKELETON || count > H2W_MAXIMUM_NODES)
		return FALSE;
	for (i = 0; i < m->node_count; i++)
	{
		map[i] = NONE;
		for (k = 0; k < count; k++)
		{
			if (!strcmp(m->nodes[i].name, skeleton[k].name))
				map[i] = (short)k;
		}
		if (map[i] == NONE)
		{
			platform_log("h2 vehicles: node '%s' is not the stand-in's", m->nodes[i].name);
			return FALSE;
		}
	}
	for (k = 0; k < count; k++)
	{
		struct h2w_node *n = &nodes[k];
		int c;

		for (i = 0; i < m->node_count && map[i] != k; i++)
			;
		if (i < m->node_count && from_model)
		{
			float const *inv = m->nodes[i].inverse;

			for (c = 0; c < 3; c++)
				world[k][c] = -(inv[3 * c] * inv[9] + inv[3 * c + 1] * inv[10] + inv[3 * c + 2] * inv[11]);
		}
		else
		{
			for (c = 0; c < 3; c++)
				world[k][c] = skeleton[k].translation[c] + (skeleton[k].parent >= 0 ? world[skeleton[k].parent][c] : 0.0f);
		}
		if (i < m->node_count)
			*n = m->nodes[i];
		else
		{
			/* (one the pieces lack: the skeleton's, unturned) */
			memset(n, 0, sizeof(*n));
			snprintf(n->name, sizeof(n->name), "%s", skeleton[k].name);
			n->rotation[3] = 1.0f;
			n->scale = 1.0f;
			n->inverse[0] = n->inverse[4] = n->inverse[8] = 1.0f;
			for (c = 0; c < 3; c++)
				n->inverse[9 + c] = -world[k][c];
		}
		n->parent = skeleton[k].parent;
		memcpy(n->translation, skeleton[k].translation, sizeof(n->translation));
		if (from_model)
		{
			for (c = 0; c < 3; c++)
				n->translation[c] = world[k][c] - (skeleton[k].parent >= 0 ? world[skeleton[k].parent][c] : 0.0f);
		}
	}
	memcpy(m->nodes, nodes, sizeof(nodes[0]) * count);
	m->node_count = count;
	for (i = 0; i < m->marker_count; i++)
		m->markers[i].node = map[m->markers[i].node];
	for (p = 0; p < m->made.part_count; p++)
	{
		struct model_part_made *part = &m->made.parts[p];
		unsigned char *side = part->vertices + (unsigned long)part->vertex_count * 32;

		for (i = 0; i < part->vertex_count; i++)
		{
			side[4 * i] = (unsigned char)map[side[4 * i]];
			side[4 * i + 1] = (unsigned char)map[side[4 * i + 1]];
		}
	}
	return TRUE;
}

/* a model's nodes' default turns made none (their inverses: no turn, less
their default places), its markers moved to stay where they were */
H2_ALIGN static void h2v_model_unturn(struct h2w_model *m)
{
	float places[H2W_MAXIMUM_NODES][3];
	long i;
	int c;

	for (i = 0; i < m->node_count; i++)
	{
		float const *inv = m->nodes[i].inverse;

		/* (the inverse takes a point v to forward*v.x + left*v.y + up*v.z +
		position: the default place is where it takes to none) */
		for (c = 0; c < 3; c++)
			places[i][c] = -(inv[3 * c] * inv[9] + inv[3 * c + 1] * inv[10] + inv[3 * c + 2] * inv[11]);
	}
	for (i = 0; i < m->marker_count; i++)
	{
		struct h2w_marker *mk = &m->markers[i];
		float const *inv;
		float d[3], world[3];

		if (mk->node < 0 || mk->node >= m->node_count)
			continue;
		inv = m->nodes[mk->node].inverse;
		for (c = 0; c < 3; c++)
			d[c] = mk->translation[c] - inv[9 + c];
		for (c = 0; c < 3; c++)
			world[c] = inv[3 * c] * d[0] + inv[3 * c + 1] * d[1] + inv[3 * c + 2] * d[2];
		for (c = 0; c < 3; c++)
			mk->translation[c] = world[c] - places[mk->node][c];
	}
	for (i = 0; i < m->node_count; i++)
	{
		float *inv = m->nodes[i].inverse;

		memset(inv, 0, sizeof(float) * 9);
		inv[0] = inv[4] = inv[8] = 1.0f;
		for (c = 0; c < 3; c++)
			inv[9 + c] = -places[i][c];
		m->nodes[i].rotation[0] = m->nodes[i].rotation[1] = m->nodes[i].rotation[2] = 0.0f;
		m->nodes[i].rotation[3] = 1.0f;
	}
}

/* a marker's place in the model's space (its node's default: as
h2v_model_unturn reads it); FALSE if the model has no marker so named */
H2_ALIGN static boolean h2v_marker_place(struct h2w_model const *m, char const *name, float place[3])
{
	long i;

	for (i = 0; i < m->marker_count; i++)
	{
		struct h2w_marker const *mk = &m->markers[i];
		float const *inv;
		float d[3];
		int c;

		if (strcmp(mk->name, name) || mk->node < 0 || mk->node >= m->node_count)
			continue;
		inv = m->nodes[mk->node].inverse;
		for (c = 0; c < 3; c++)
			d[c] = mk->translation[c] - inv[9 + c];
		for (c = 0; c < 3; c++)
			place[c] = inv[3 * c] * d[0] + inv[3 * c + 1] * d[1] + inv[3 * c + 2] * d[2];
		return TRUE;
	}
	return FALSE;
}

/* ---------- private code: a vehicle on its own rig (own_graph) */

/* a Halo 2 quaternion's turn (x, y, z, w) as a 3x3 (rows), the convention
(0 the formula's, 1 transposed) the rig's own inverses agree with */
H2_ALIGN static void h2v_quaternion_matrix(float const *q, int convention, float *m)
{
	float x = q[0], y = q[1], z = q[2], w = q[3];
	float r[9];
	int i, j;

	r[0] = 1.0f - 2.0f * (y * y + z * z); r[1] = 2.0f * (x * y - z * w); r[2] = 2.0f * (x * z + y * w);
	r[3] = 2.0f * (x * y + z * w); r[4] = 1.0f - 2.0f * (x * x + z * z); r[5] = 2.0f * (y * z - x * w);
	r[6] = 2.0f * (x * z - y * w); r[7] = 2.0f * (y * z + x * w); r[8] = 1.0f - 2.0f * (x * x + y * y);
	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			m[3 * i + j] = convention ? r[3 * j + i] : r[3 * i + j];
	}
}

/* (its inverse) a 3x3 as a Halo 2 quaternion of the convention */
H2_ALIGN static void h2v_matrix_quaternion(float const *in, int convention, float *q)
{
	float m[9], trace;
	int i, j;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			m[3 * i + j] = convention ? in[3 * j + i] : in[3 * i + j];
	}
	trace = m[0] + m[4] + m[8];
	if (trace > 0.0f)
	{
		float r = sqrtf(trace + 1.0f) * 2.0f;

		q[3] = 0.25f * r; q[0] = (m[7] - m[5]) / r; q[1] = (m[2] - m[6]) / r; q[2] = (m[3] - m[1]) / r;
	}
	else if (m[0] > m[4] && m[0] > m[8])
	{
		float r = sqrtf(1.0f + m[0] - m[4] - m[8]) * 2.0f;

		q[3] = (m[7] - m[5]) / r; q[0] = 0.25f * r; q[1] = (m[1] + m[3]) / r; q[2] = (m[2] + m[6]) / r;
	}
	else if (m[4] > m[8])
	{
		float r = sqrtf(1.0f + m[4] - m[0] - m[8]) * 2.0f;

		q[3] = (m[2] - m[6]) / r; q[0] = (m[1] + m[3]) / r; q[1] = 0.25f * r; q[2] = (m[5] + m[7]) / r;
	}
	else
	{
		float r = sqrtf(1.0f + m[8] - m[0] - m[4]) * 2.0f;

		q[3] = (m[3] - m[1]) / r; q[0] = (m[2] + m[6]) / r; q[1] = (m[5] + m[7]) / r; q[2] = 0.25f * r;
	}
}

/* a = b c (3x3, rows) */
H2_ALIGN static void h2v_matrix_multiply(float const *b, float const *c, float *a)
{
	float o[9];
	int i, j;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			o[3 * i + j] = b[3 * i] * c[j] + b[3 * i + 1] * c[3 + j] + b[3 * i + 2] * c[6 + j];
	}
	memcpy(a, o, sizeof(o));
}

H2_ALIGN static void h2v_matrix_transpose(float const *b, float *a)
{
	float o[9];
	int i, j;

	for (i = 0; i < 3; i++)
	{
		for (j = 0; j < 3; j++)
			o[3 * i + j] = b[3 * j + i];
	}
	memcpy(a, o, sizeof(o));
}

/* (before the rig is unturned) each node's Halo 2 frame: its turn in the
model (its inverse's: local to model, as h2v_marker_place reads it), its
Halo 2 parent's (none: a piece's root, the vehicle's), its default
translation; and the quaternion convention its default turns agree with */
H2_ALIGN static void h2v_native_begin(struct h2v_vehicle *v)
{
	struct h2w_model *m = &v->model;
	float error[2] = { 0.0f, 0.0f };
	long n;
	int c;

	for (n = 0; n < m->node_count; n++)
	{
		struct h2v_native_node *o = &v->native[n];
		short parent = m->nodes[n].parent;

		memcpy(o->W, m->nodes[n].inverse, sizeof(o->W));
		memcpy(o->Dt, m->nodes[n].translation, sizeof(o->Dt));
		if (parent >= 0 && parent < n)
			memcpy(o->Wp, m->nodes[parent].inverse, sizeof(o->Wp));
		else
		{
			memset(o->Wp, 0, sizeof(o->Wp));
			o->Wp[0] = o->Wp[4] = o->Wp[8] = 1.0f;
		}
		for (c = 0; c < 2; c++)
		{
			float r[9], w[9];
			int k;

			h2v_quaternion_matrix(m->nodes[n].rotation, c, r);
			h2v_matrix_multiply(o->Wp, r, w);
			for (k = 0; k < 9; k++)
				error[c] += (w[k] - o->W[k]) * (w[k] - o->W[k]);
		}
	}
	v->convention = error[1] < error[0] ? 1 : 0;
	platform_log("h2 vehicles: %s: its own rig, %ld nodes (Halo 2's turns agree %s: %.4f against %.4f)", v->name, m->node_count,
		v->convention ? "transposed" : "as written", error[v->convention], error[!v->convention]);
}

/* (after it is unturned) a piece's root the hull's child, each node's
translation its place less its parent's */
H2_ALIGN static void h2v_native_finish(struct h2v_vehicle *v)
{
	struct h2w_model *m = &v->model;
	float places[H2W_MAXIMUM_NODES][3];
	long n;
	int c;

	for (n = 0; n < m->node_count; n++)
	{
		for (c = 0; c < 3; c++)
			places[n][c] = -m->nodes[n].inverse[9 + c];
	}
	for (n = 0; n < m->node_count; n++)
	{
		struct h2w_node *node = &m->nodes[n];

		if (n > 0 && (node->parent < 0 || node->parent >= n))
			node->parent = 0;
		for (c = 0; c < 3; c++)
			node->translation[c] = places[n][c] - (n > 0 ? places[node->parent][c] : 0.0f);
		memcpy(v->native[n].t, node->translation, sizeof(v->native[n].t));
	}
}

/* (own graph) a jmad animation decoded (h2w_animation_decode), else as a
vehicle's base poses keep it: their node flags (static rotation,
translation, scale, then animated) not at the data's end but right after
its animated codec's data - found as the place whose flags' bits count
the codecs' nodes (a Banshee's canopy, a Wraith's hatch) */
H2_ALIGN static long h2v_bits(unsigned char const *p, long bytes)
{
	long count = 0, i;

	for (i = 0; i < bytes; i++)
	{
		unsigned v = p[i];

		while (v)
			count += v & 1, v >>= 1;
	}
	return count;
}

H2_ALIGN static boolean h2v_animation_decode(struct build *build, unsigned long entry, long node_count, struct h2w_samples *s)
{
	struct h2_cache *cache = build->cache;
	unsigned char const *e, *d;
	unsigned long size, static_size, at;
	long static_bytes, animated_bytes, each;

	if (h2w_animation_decode(build, entry, node_count, s))
		return TRUE;
	if (!(e = h2_cache_at(cache, entry, 0x60)))
		return FALSE;
	memset(s, 0, sizeof(*s));
	s->frames = read16(e + 0x14);
	size = read32(e + 0x28);
	static_bytes = e[0x30];
	animated_bytes = e[0x31];
	static_size = (unsigned short)read16(e + 0x36);
	each = static_bytes / 3;
	if (s->frames < 1 || s->frames > 2048 || !size || size > 4 * 1024 * 1024 || static_bytes != animated_bytes || (static_bytes % 3) ||
		!each || static_size + 0x30 > size || !(d = h2_cache_at(cache, read32(e + 0x2C), size)))
	{
		return FALSE;
	}
	for (at = static_size + 0x30; at + (unsigned long)(static_bytes + animated_bytes) <= size; at += 4)
	{
		unsigned char const *f = d + at;
		int c;
		boolean fits = TRUE;

		for (c = 0; c < 3 && fits; c++)
		{
			fits = h2v_bits(f + each * c, each) == (static_size ? d[1 + c] : 0) &&
				h2v_bits(f + static_bytes + each * c, each) == d[static_size + 1 + c];
		}
		if (!fits || h2v_bits(f + static_bytes, animated_bytes) == 0)
			continue;
		if ((static_size && !h2w_codec_read(d, size, 0, f, each, FALSE, s)) ||
			!h2w_codec_read(d, size, static_size, f + static_bytes, each, TRUE, s))
		{
			h2w_samples_free(s);
			memset(s, 0, sizeof(*s));
			s->frames = read16(e + 0x14);
			continue;
		}
		return TRUE;
	}
	return FALSE;
}

enum { H2V_HATCH_FUNCTION = 3 };   /* (the hatch's object function: D out) */

/* (own graph) its hatch or canopy: Halo 2's "combat:opening" (a base pose,
here already the rig's) made an overlay - each node's turn and translation
from the opening's first frame (the shut hatch), so the vehicle's object
overlay plays it over whatever pose it has (objects.c: rotation overlay
times base, translation added), its frame the hatch function (D out,
h2_vehicles_write) */
H2_ALIGN static void h2v_hatch_make(struct h2w_graph *graph, struct h2v_vehicle *v, struct h2w_samples const *base, long piece)
{
	struct h2w_samples o;
	struct h2w_animation *a;
	char name[48];
	long n, f;
	boolean any = FALSE;

	if (v->hatch_count >= 2 || graph->animation_count >= H2W_MAXIMUM_ANIMATIONS || base->frames < 2)
		return;
	memset(&o, 0, sizeof(o));
	o.frames = base->frames;
	for (n = 0; n < graph->node_count; n++)
	{
		if (base->rotation[n] && base->rotation_animated[n] && (o.rotation[n] = malloc(sizeof(float) * 4 * (size_t)o.frames)))
		{
			float m0[9], m0t[9];

			h2v_quaternion_matrix(base->rotation[n], v->convention, m0);
			h2v_matrix_transpose(m0, m0t);
			for (f = 0; f < o.frames; f++)
			{
				float r[9];

				h2v_quaternion_matrix(base->rotation[n] + 4 * f, v->convention, r);
				h2v_matrix_multiply(r, m0t, r);
				h2v_matrix_quaternion(r, v->convention, o.rotation[n] + 4 * f);
			}
			o.rotation_animated[n] = TRUE;
			any = TRUE;
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 vehicles: %s: hatch turns node %ld '%s'", v->name, n, graph->nodes[n].name);
		}
		if (base->translation[n] && base->translation_animated[n] && (o.translation[n] = malloc(sizeof(float) * 3 * (size_t)o.frames)))
		{
			for (f = 0; f < o.frames; f++)
			{
				int c;

				for (c = 0; c < 3; c++)
					o.translation[n][3 * f + c] = base->translation[n][3 * f + c] - base->translation[n][c];
			}
			o.translation_animated[n] = TRUE;
			any = TRUE;
		}
	}
	snprintf(name, sizeof(name), "%ld:hatch", piece);
	a = &graph->animations[graph->animation_count];
	if (any && h2w_animation_make(graph, &v->model, &o, _animation_overlay, name, a))
	{
		a->slot = NONE;
		a->sound = a->graph_sound = NONE;
		a->second_key_frame = NONE;
		v->has_screen[graph->animation_count] = FALSE;
		v->hatches[v->hatch_count++] = (short)graph->animation_count;
		graph->animation_count++;
		v->physics.has_hatch = TRUE;
		v->physics.hatch_seconds[0] = (float)base->frames / 30.0f;
	}
	h2w_samples_free(&o);
}

/* (own graph) the vehicle's graph from Halo 2's: each piece's jmad (its
render model's name), every base and overlay animation, its nodes the
rig's by name, named "<piece>:<Halo 2's name>"; each turn moved into the
unturned rig (a base pose's: Wp R W^T - the node's turn from its default
in its parent's model frame -; an overlay's: W R W^T - the turn about
the node's own default frame), each translation its parent's frame's (a
base pose's from its default here: t + Wp (t' - Dt)) */
H2_ALIGN static struct h2w_graph *h2v_graph_make(struct build *build, struct h2v_vehicle *v)
{
	struct h2_cache *cache = build->cache;
	struct h2v_kind const *k = &h2v_kinds[v->kind];
	struct h2w_graph *graph = calloc(1, sizeof(*graph));
	long n, p;

	if (!graph)
		return NULL;
	graph->node_count = v->model.node_count;
	for (n = 0; n < graph->node_count; n++)
	{
		long c;

		snprintf(graph->nodes[n].name, sizeof(graph->nodes[n].name), "%s", v->model.nodes[n].name);
		graph->nodes[n].parent = v->model.nodes[n].parent;
		graph->nodes[n].child = graph->nodes[n].sibling = NONE;
		graph->nodes[n].source = (short)n;
		for (c = 0; c < graph->node_count && graph->nodes[n].child == NONE; c++)
		{
			if (c != n && v->model.nodes[c].parent == n)
				graph->nodes[n].child = (short)c;
		}
		for (c = n + 1; c < graph->node_count; c++)
		{
			if (v->model.nodes[c].parent == v->model.nodes[n].parent)
			{
				graph->nodes[n].sibling = (short)c;
				break;
			}
		}
	}
	for (p = 0; p < H2V_MAXIMUM_PIECES && k->pieces[p].render_model; p++)
	{
		long jmad = h2v_tag_named(cache, 'jmad', k->pieces[p].render_model), node_count, animation_count, screen_count, i;
		unsigned long nodes, animations, screens = 0;
		short map[H2W_MAXIMUM_GRAPH_NODES];
		struct h2_cache_tag t;

		if (jmad == NONE || !h2_cache_tag_get(cache, jmad, &t) || !h2_cache_block(cache, t.address + 0xC, 0x20, &node_count, &nodes) ||
			!h2_cache_block(cache, t.address + 0x2C, 0x60, &animation_count, &animations) || node_count < 1 || node_count > H2W_MAXIMUM_GRAPH_NODES)
		{
			continue;
		}
		if (!h2_cache_block(cache, t.address + 0x24, 0x1C, &screen_count, &screens))
			screen_count = 0;
		for (i = 0; i < node_count; i++)
		{
			char name[32];

			h2w_name(cache, h2_cache_read32(cache, nodes + 0x20 * (unsigned long)i), name, sizeof(name));
			map[i] = h2w_model_node(&v->model, name);
		}
		for (i = 0; i < animation_count && graph->animation_count < H2W_MAXIMUM_ANIMATIONS; i++)
		{
			unsigned long entry = animations + 0x60 * (unsigned long)i;
			unsigned char const *e = h2_cache_at(cache, entry, 0x60);
			struct h2w_samples s, kept;
			char h2_name[48], name[48];
			int type, screen;
			boolean starts_at_rest;
			struct h2w_animation *a;

			if (!e || (type = e[0x10]) > 1)
				continue;
			h2_cache_string(cache, read32(e), h2_name, sizeof(h2_name));
			snprintf(name, sizeof(name), "%ld:%s", p, h2_name);
			if (!h2v_animation_decode(build, entry, node_count, &s))
			{
				if (getenv("HALO_H2_CODEC_DEBUG"))
				{
					unsigned long size = read32(e + 0x28), at;
					unsigned char const *d = h2_cache_at(cache, read32(e + 0x2C), size);
					char hex[1200];
					int length = 0;

					for (at = 0; d && at < size && length < 1100; at++)
						length += snprintf(hex + length, sizeof(hex) - length, "%02x%s", d[at], (at % 16) == 15 ? "|" : "");
					platform_log("h2 vehicles: data %s", hex);
					platform_log("h2 vehicles: entry 0x30.. %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x",
						e[0x30], e[0x31], e[0x32], e[0x33], e[0x34], e[0x35], e[0x36], e[0x37], e[0x38], e[0x39], e[0x3a], e[0x3b], e[0x3c], e[0x3d], e[0x3e], e[0x3f]);
				}
				platform_log("h2 vehicles: %s: animation %s not decoded (frames %d size %lu at %lx flags %d %d static %d, codec %d)", v->name, name,
					(int)read16(e + 0x14), (unsigned long)read32(e + 0x28), (unsigned long)read32(e + 0x2C), (int)e[0x30], (int)e[0x31],
					(int)read16(e + 0x36), h2_cache_at(cache, read32(e + 0x2C) + read16(e + 0x36), 1) ? (int)*(unsigned char const *)h2_cache_at(cache, read32(e + 0x2C) + read16(e + 0x36), 1) : -1);
				continue;
			}
			/* (a hatch's animation that starts at the rest pose - every turned
			node's first frame Halo 2's identity: the one that moves the hatch
			away from its rest, h2v_hatch_make) */
			starts_at_rest = TRUE;
			for (n = 0; n < node_count; n++)
			{
				if (s.rotation[n] && s.rotation_animated[n] && fabsf(s.rotation[n][3]) < 0.9995f)
					starts_at_rest = FALSE;
			}
			memset(&kept, 0, sizeof(kept));
			kept.frames = s.frames;
			for (n = 0; n < node_count; n++)
			{
				short m = map[n];
				struct h2v_native_node const *o;
				long f;

				if (m < 0 || m >= graph->node_count || kept.rotation[m] || kept.translation[m] || kept.scale[m])
					continue;
				o = &v->native[m];
				if (s.rotation[n])
				{
					float wt[9];

					h2v_matrix_transpose(o->W, wt);
					for (f = 0; f < s.frames; f++)
					{
						float r[9];
						float *q = s.rotation[n] + 4 * f;

						h2v_quaternion_matrix(q, v->convention, r);
						if (type == 1)
							h2v_matrix_multiply(o->W, r, r);
						else
							h2v_matrix_multiply(o->Wp, r, r);
						h2v_matrix_multiply(r, wt, r);
						h2v_matrix_quaternion(r, v->convention, q);
					}
					kept.rotation[m] = s.rotation[n], s.rotation[n] = NULL;
					kept.rotation_animated[m] = s.rotation_animated[n];
				}
				if (s.translation[n])
				{
					for (f = 0; f < s.frames; f++)
					{
						float *t = s.translation[n] + 3 * f, d[3];
						int c;

						for (c = 0; c < 3; c++)
							d[c] = type == 1 ? t[c] : t[c] - o->Dt[c];
						for (c = 0; c < 3; c++)
							t[c] = (type == 1 ? 0.0f : o->t[c]) + o->Wp[3 * c] * d[0] + o->Wp[3 * c + 1] * d[1] + o->Wp[3 * c + 2] * d[2];
					}
					kept.translation[m] = s.translation[n], s.translation[n] = NULL;
					kept.translation_animated[m] = s.translation_animated[n];
				}
				if (s.scale[n])
				{
					kept.scale[m] = s.scale[n], s.scale[n] = NULL;
					kept.scale_animated[m] = s.scale_animated[n];
				}
			}
			h2w_samples_free(&s);
			a = &graph->animations[graph->animation_count];
			if (!h2w_animation_make(graph, &v->model, &kept, type == 1 ? _animation_overlay : _animation_base, name, a))
			{
				platform_log("h2 vehicles: %s: animation %s not made", v->name, name);
				h2w_samples_free(&kept);
				continue;
			}
			a->slot = NONE;
			a->sound = a->graph_sound = NONE;
			a->second_key_frame = NONE;
			/* (its blend screen: its aiming bounds) */
			screen = (signed char)e[0x12];
			v->has_screen[graph->animation_count] = FALSE;
			if (screen >= 0 && screen < screen_count)
			{
				unsigned char const *b = h2_cache_at(cache, screens + 0x1C * (unsigned long)screen, 0x1C);

				if (b)
				{
					memcpy(v->screens[graph->animation_count], b + 4, 0x18);
					v->has_screen[graph->animation_count] = TRUE;
				}
			}
			graph->animation_count++;
			/* (its hatch: of "combat:opening" and "combat:closing" the one
			that starts at the rest pose made an overlay - the Scorpion's and
			the Spectre's opening (their rest shut: open while a rider gets in
			or out, has_hatch 1), the Banshee's closing (its rest the canopy
			open: shut while a rider sits, has_hatch 2, as Halo 2 holds the
			closing's last frame until the rider leaves and the opening plays
			back to rest); the other's length the way back. HALO_H2_NO_HATCH
			none; HALO_H2_HATCH_OLD: the opening always, as before) */
			if (type == 0 && (!strcmp(h2_name, "combat:opening") || !strcmp(h2_name, "combat:closing")) && !getenv("HALO_H2_NO_HATCH"))
			{
				boolean closing = !strcmp(h2_name, "combat:closing");

				if (closing ? starts_at_rest && !v->physics.has_hatch && !getenv("HALO_H2_HATCH_OLD") : v->physics.has_hatch != 2)
				{
					h2v_hatch_make(graph, v, &kept, p);
					if (v->physics.has_hatch && closing)
						v->physics.has_hatch = 2;
					platform_log("h2 vehicles: %s: its hatch the %s's overlay (%s at rest)", v->name, h2_name,
						v->physics.has_hatch == 2 ? "open" : "shut");
				}
				else
					v->physics.hatch_seconds[1] = (float)kept.frames / 30.0f;
			}
			h2w_samples_free(&kept);
		}
	}
	platform_log("h2 vehicles: %s: its graph Halo 2's, %ld nodes, %ld animations", v->name, graph->node_count, graph->animation_count);
	if (getenv("HALO_H2_DEBUG"))
	{
		for (n = 0; n < graph->animation_count; n++)
			platform_log("h2 vehicles: %s: animation %ld '%s' type %d, %d frames%s", v->name, n, graph->animations[n].name,
				(int)graph->animations[n].type, (int)graph->animations[n].frame_count, v->has_screen[n] ? ", a screen" : "");
	}
	if (!graph->animation_count)
	{
		h2w_graph_free(graph);
		return NULL;
	}
	return graph;
}

/* (h2v_physics_read) a rigid body's place (its node's space, or the model's
for node -1) in the model's space: its node's default, as its inverse - the
render model's node's 0x2C - gives it */
static void h2v_body_place(struct h2_cache *cache, unsigned long nodes, long node_count, long node, float const *local, float *place)
{
	int c;

	if (node >= 0 && node < node_count)
	{
		float inv[12], d[3];
		int q;

		for (q = 0; q < 12; q++)
			inv[q] = h2w_read_real(cache, nodes + 0x60 * (unsigned long)node + 0x2C + 4 * (unsigned long)q);
		for (c = 0; c < 3; c++)
			d[c] = local[c] - inv[9 + c];
		for (c = 0; c < 3; c++)
			place[c] = inv[3 * c] * d[0] + inv[3 * c + 1] * d[1] + inv[3 * c + 2] * d[2];
	}
	else
	{
		for (c = 0; c < 3; c++)
			place[c] = local[c];
	}
}

/* (h2v_hull_read) one shape of a rigid body into the hull: type 2 a box
(0xB0 each at 0x60: half extents +0x40, rotation +0x70.., translation +0xA0),
4 a polyhedron (0x120 each at 0x70: its vertices - count +0x100 - in rows of
four x, y, z, 0x30 a row, +0x68 rows; up to three rows inside the shape at
+0x70, more in the block at 0x78, each polyhedron's after the last's) */
static void h2v_hull_shape_read(struct h2_cache *cache, struct h2vp_definition *p, unsigned long phmo, short type, short index,
	unsigned long nodes, long node_count, long node)
{
	struct h2vp_hull_shape *shape;
	float (*out)[3] = p->hull_vertices;
	long used = 0, s, count = 0;
	unsigned long block = 0, at;

	for (s = 0; s < p->hull_shape_count; s++)
		used += p->hull_shapes[s].vertex_count;
	if (p->hull_shape_count >= H2VP_MAXIMUM_HULL_SHAPES)
		return;
	shape = &p->hull_shapes[p->hull_shape_count];
	if (type == 2)
	{
		float half[3], axes[9], move[3];
		int c, corner;

		if (!h2_cache_block(cache, phmo + 0x60, 0xB0, &count, &block) || index < 0 || index >= count || used + 8 > H2VP_MAXIMUM_HULL_VERTICES)
			return;
		at = block + 0xB0 * (unsigned long)index;
		for (c = 0; c < 3; c++)
		{
			half[c] = h2w_read_real(cache, at + 0x40 + 4 * (unsigned long)c);
			move[c] = h2w_read_real(cache, at + 0xA0 + 4 * (unsigned long)c);
			axes[c] = h2w_read_real(cache, at + 0x70 + 4 * (unsigned long)c);
			axes[3 + c] = h2w_read_real(cache, at + 0x80 + 4 * (unsigned long)c);
			axes[6 + c] = h2w_read_real(cache, at + 0x90 + 4 * (unsigned long)c);
		}
		for (corner = 0; corner < 8; corner++)
		{
			float local[3], x = corner & 1 ? half[0] : -half[0], y = corner & 2 ? half[1] : -half[1], z = corner & 4 ? half[2] : -half[2];

			for (c = 0; c < 3; c++)
				local[c] = axes[c] * x + axes[3 + c] * y + axes[6 + c] * z + move[c];
			h2v_body_place(cache, nodes, node_count, node, local, out[used + corner]);
		}
		shape->vertex_count = 8;
	}
	else if (type == 4)
	{
		unsigned long rows, external = 0;
		long external_count = 0, first_row = 0, q, row_count, vertices, v;

		if (!h2_cache_block(cache, phmo + 0x70, 0x120, &count, &block) || index < 0 || index >= count)
			return;
		h2_cache_block(cache, phmo + 0x78, 0x30, &external_count, &external);
		for (q = 0; q < index; q++)
		{
			long size = (long)h2_cache_read32(cache, block + 0x120 * (unsigned long)q + 0x68);

			if (size > 3)
				first_row += size;
		}
		at = block + 0x120 * (unsigned long)index;
		row_count = (long)h2_cache_read32(cache, at + 0x68);
		vertices = (long)h2_cache_read32(cache, at + 0x100);
		if (row_count <= 0 || vertices < 4 || vertices > row_count * 4 || used + vertices > H2VP_MAXIMUM_HULL_VERTICES ||
			(row_count > 3 && first_row + row_count > external_count))
		{
			platform_log("h2 vehicles: physics model polyhedron %d: %ld rows, %ld vertices (block row %ld of %ld): left out", (int)index, row_count,
				vertices, first_row, external_count);
			return;
		}
		rows = row_count > 3 ? external + 0x30 * (unsigned long)first_row : at + 0x70;
		for (v = 0; v < vertices; v++)
		{
			float local[3];
			int c;

			for (c = 0; c < 3; c++)
				local[c] = h2w_read_real(cache, rows + 0x30 * (unsigned long)(v / 4) + 0x10 * (unsigned long)c + 4 * (unsigned long)(v % 4));
			h2v_body_place(cache, nodes, node_count, node, local, out[used + v]);
		}
		shape->vertex_count = vertices;
	}
	else
	{
		platform_log("h2 vehicles: physics model shape of type %d: left out of the hull", (int)type);
		return;
	}
	shape->first_vertex = used;
	shape->friction = h2w_read_real(cache, at + 0xC);
	shape->restitution = h2w_read_real(cache, at + 0x10);
	p->hull_shape_count++;
}

/* whether a rigid body's permutation is its region's living one: "base"
(the Scorpion's physics model has a whole second hull as "destroyed"), or
the region's only one: regions 0xC0 (0xC each: name, permutations) */
static boolean h2v_body_living(struct h2_cache *cache, unsigned long phmo, short region, short permutation)
{
	long regions = 0, permutations = 0;
	unsigned long regions_at = 0, permutations_at = 0;
	char name[64];

	if (!h2_cache_block(cache, phmo + 0xC0, 0xC, &regions, &regions_at) || region < 0 || region >= regions ||
		!h2_cache_block(cache, regions_at + 0xC * (unsigned long)region + 4, 0xC, &permutations, &permutations_at))
	{
		return TRUE;
	}
	if (permutations <= 1 || permutation < 0 || permutation >= permutations)
		return TRUE;
	return !h2_cache_string(cache, h2_cache_read32(cache, permutations_at + 0xC * (unsigned long)permutation), name, sizeof(name)) ||
		!strcmp(name, "base");
}

/* its rigid hull (h2vp_definition's hull_*), from the physics model's
rigid bodies (0x38, 0xA0 each: node +0, region +2, permutation +4, shape type
+0x38 and index +0x3A - 14 a list, 0x68 each at 0x90: its count +0x20, its
children's type and index 0x10 apart from +0x28 -, mass +0x40, centre of mass
+0x50, inertia +0x60, +0x70, +0x80, each row 0x10) */
static void h2v_hull_read(struct h2_cache *cache, struct h2v_vehicle *v, struct h2vp_definition *p, unsigned long phmo, unsigned long nodes,
	long node_count)
{
	long bodies = 0, b;
	unsigned long body = 0;
	float mass_total = 0.0f, sum[3] = { 0.0f, 0.0f, 0.0f }, inertia[9];
	int c, r;

	p->hull_shape_count = 0;
	p->hull_mass = 0.0f;
	memset(inertia, 0, sizeof(inertia));
	if (!h2_cache_block(cache, phmo + 0x38, 0xA0, &bodies, &body))
		return;
	for (b = 0; b < bodies; b++)
	{
		unsigned long at = body + 0xA0 * (unsigned long)b;
		unsigned long first = h2_cache_read32(cache, at), second = h2_cache_read32(cache, at + 4);
		short node = (short)(first & 0xFFFF), region = (short)(first >> 16), permutation = (short)(second & 0xFFFF);
		float mass = h2w_read_real(cache, at + 0x40), local[3], place[3];

		if (mass <= 0.0f || !h2v_body_living(cache, phmo, region, permutation))
			continue;
		for (c = 0; c < 3; c++)
			local[c] = h2w_read_real(cache, at + 0x50 + 4 * (unsigned long)c);
		h2v_body_place(cache, nodes, node_count, node, local, place);
		for (c = 0; c < 3; c++)
			sum[c] += place[c] * mass;
		mass_total += mass;
	}
	if (mass_total <= 0.0f)
		return;
	for (c = 0; c < 3; c++)
		p->hull_center[c] = sum[c] / mass_total;
	p->hull_mass = mass_total;
	for (b = 0; b < bodies; b++)
	{
		unsigned long at = body + 0xA0 * (unsigned long)b;
		unsigned long first = h2_cache_read32(cache, at), second = h2_cache_read32(cache, at + 4), shape_word = h2_cache_read32(cache, at + 0x38);
		short node = (short)(first & 0xFFFF), region = (short)(first >> 16), permutation = (short)(second & 0xFFFF);
		short type = (short)(shape_word & 0xFFFF), index = (short)(shape_word >> 16);
		float mass = h2w_read_real(cache, at + 0x40), local[3], place[3], d[3], squared;

		if (mass <= 0.0f || !h2v_body_living(cache, phmo, region, permutation))
			continue;
		/* (its damping, the heaviest's, +0x24 and +0x28: the kit's field
		order - inertia scale, linear, angular damping, centre of mass
		offset (+0x2C): 0.18 0 -0.16 on the Warthog -, not the +0x28, +0x2C
		h2v_physics_read takes) */
		if (mass >= mass_total * 0.5f)
		{
			p->hull_linear_damping = h2w_read_real(cache, at + 0x24);
			p->hull_angular_damping = h2w_read_real(cache, at + 0x28);
		}
		/* (its inertia, and its mass's about the hull's centre) */
		for (c = 0; c < 3; c++)
			local[c] = h2w_read_real(cache, at + 0x50 + 4 * (unsigned long)c);
		h2v_body_place(cache, nodes, node_count, node, local, place);
		for (c = 0; c < 3; c++)
			d[c] = place[c] - p->hull_center[c];
		squared = d[0] * d[0] + d[1] * d[1] + d[2] * d[2];
		for (r = 0; r < 3; r++)
		{
			for (c = 0; c < 3; c++)
			{
				inertia[3 * r + c] += h2w_read_real(cache, at + 0x60 + 0x10 * (unsigned long)r + 4 * (unsigned long)c) +
					mass * ((r == c ? squared : 0.0f) - d[r] * d[c]);
			}
		}
		if (type == 14)
		{
			long lists = 0, l, count;
			unsigned long list_block = 0, list;

			if (!h2_cache_block(cache, phmo + 0x90, 0x68, &lists, &list_block) || index < 0 || index >= lists)
				continue;
			list = list_block + 0x68 * (unsigned long)index;
			count = (long)h2_cache_read32(cache, list + 0x20);
			if (count > 4)
				platform_log("h2 vehicles: %s: a physics model list of %ld shapes: the first 4 of it", v->name, count);
			for (l = 0; l < count && l < 4; l++)
			{
				unsigned long child = h2_cache_read32(cache, list + 0x28 + 0x10 * (unsigned long)l);

				h2v_hull_shape_read(cache, p, phmo, (short)(child & 0xFFFF), (short)(child >> 16), nodes, node_count, node);
			}
		}
		else
			h2v_hull_shape_read(cache, p, phmo, type, index, nodes, node_count, node);
	}
	memcpy(p->hull_inertia, inertia, sizeof(inertia));
	platform_log("h2 vehicles: %s: rigid hull: mass %.1f (the model's %.1f), centre %.3f %.3f %.3f, inertia %.1f %.1f %.1f, %ld shapes",
		v->name, p->hull_mass, p->mass, p->hull_center[0], p->hull_center[1], p->hull_center[2], inertia[0], inertia[4], inertia[8], p->hull_shape_count);
	if (getenv("HALO_H2_DEBUG"))
	{
		long s;

		for (s = 0; s < p->hull_shape_count; s++)
		{
			struct h2vp_hull_shape const *shape = &p->hull_shapes[s];
			float low[3] = { 1e9f, 1e9f, 1e9f }, high[3] = { -1e9f, -1e9f, -1e9f };
			long i;

			for (i = 0; i < shape->vertex_count; i++)
			{
				for (c = 0; c < 3; c++)
				{
					float value = p->hull_vertices[shape->first_vertex + i][c];

					low[c] = value < low[c] ? value : low[c];
					high[c] = value > high[c] ? value : high[c];
				}
			}
			platform_log("h2 vehicles: %s: hull shape %ld: %ld vertices, %.3f %.3f %.3f .. %.3f %.3f %.3f, friction %.2f restitution %.2f", v->name, s,
				shape->vertex_count, low[0], low[1], low[2], high[0], high[1], high[2], shape->friction, shape->restitution);
		}
	}
}

/* (h2v_physics_read) its death's push: its hlmt's damage info (0x60,
0xF8 each) damage sections (0xBC, 0x38 each: name, flags, vitality, instant
responses at 0xC, 0x50 each: new state 0x20 - 4 destroyed -, transition
effect 0xC); the first destroyed response with a transition effect whose
events (0x14, 0x38 each) have an acceleration (0x28, 0x14 each: location
4, acceleration 8, inner and outer cone 0xC, 0x10) at a location (the
effect's 0xC, a marker's name each) the model has */
/* (the force model's reading's log: quiet when it is read again as a level
loads, h2v_force_model_scalars_write - with no model, its markers' notes
are not news) */
static boolean h2v_physics_quiet;
#define h2v_physics_log(...) do { if (!h2v_physics_quiet) platform_log(__VA_ARGS__); } while (0)

H2_ALIGN static void h2v_death_push_read(struct h2_cache *cache, struct h2v_vehicle *v, struct h2vp_definition *p)
{
	long hlmt = h2v_tag_named(cache, 'hlmt', h2v_kinds[v->kind].pieces[0].render_model);
	struct h2_cache_tag t;
	long count, section_count, s;
	unsigned long info, sections;

	if (hlmt == NONE || !h2_cache_tag_get(cache, hlmt, &t) || !h2_cache_block(cache, t.address + 0x60, 0xF8, &count, &info) || count < 1 ||
		!h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections))
	{
		return;
	}
	for (s = 0; s < section_count && !p->has_death_push; s++)
	{
		long response_count, r;
		unsigned long responses;

		if (!h2_cache_block(cache, sections + 0x38 * (unsigned long)s + 0xC, 0x50, &response_count, &responses))
			continue;
		for (r = 0; r < response_count && !p->has_death_push; r++)
		{
			unsigned long response = responses + 0x50 * (unsigned long)r;
			long effect = h2_cache_reference(cache, response + 0xC), event_count, e, location_count;
			unsigned long events, locations;
			struct h2_cache_tag et;

			if ((h2_cache_read32(cache, response + 0x20) & 0xFFFF) != 4 || effect == NONE || !h2_cache_tag_get(cache, effect, &et) ||
				et.group_tag != 'effe' || !h2_cache_block(cache, et.address + 0x14, 0x38, &event_count, &events) ||
				!h2_cache_block(cache, et.address + 0xC, 4, &location_count, &locations))
			{
				continue;
			}
			for (e = 0; e < event_count && !p->has_death_push; e++)
			{
				long acceleration_count, a;
				unsigned long accelerations;

				if (!h2_cache_block(cache, events + 0x38 * (unsigned long)e + 0x28, 0x14, &acceleration_count, &accelerations))
					continue;
				for (a = 0; a < acceleration_count && !p->has_death_push; a++)
				{
					unsigned long at = accelerations + 0x14 * (unsigned long)a;
					short location = (short)((h2_cache_read32(cache, at + 4)) & 0xFFFF);
					float acceleration = h2w_read_real(cache, at + 8);
					char marker[64];
					long m;

					if (acceleration == 0.0f || fabsf(acceleration) > 50.0f || location < 0 || location >= location_count)
						continue;
					h2w_name(cache, h2_cache_read32(cache, locations + 4 * (unsigned long)location), marker, sizeof(marker));
					for (m = 0; m < v->model.marker_count && strcmp(v->model.markers[m].name, marker); m++)
						;
					if (m == v->model.marker_count || !h2v_marker_place(&v->model, marker, p->death_push_position))
						continue;
					/* (the marker's direction: its turn's forward, in its
					node's default - as h2v_marker_place reads its place) */
					{
						struct h2w_marker const *mk = &v->model.markers[m];
						float const *inv = v->model.nodes[mk->node].inverse;
						float const *q = mk->rotation;
						float local[3], world[3];
						int c;

						/* (the quaternion's forward axis: x) */
						local[0] = 1.0f - 2.0f * (q[1] * q[1] + q[2] * q[2]);
						local[1] = 2.0f * (q[0] * q[1] + q[2] * q[3]);
						local[2] = 2.0f * (q[0] * q[2] - q[1] * q[3]);
						for (c = 0; c < 3; c++)
							world[c] = inv[3 * c] * local[0] + inv[3 * c + 1] * local[1] + inv[3 * c + 2] * local[2];
						for (c = 0; c < 3; c++)
							p->death_push_direction[c] = world[c];
					}
					p->death_push_acceleration = acceleration;
					p->death_push_inner = h2w_read_real(cache, at + 0xC);
					p->death_push_cone = h2w_read_real(cache, at + 0x10);
					p->has_death_push = 1;
					h2v_physics_log("h2 vehicles: %s: its death pushes it %.2f at '%s' (%.2f %.2f %.2f, facing %.2f %.2f %.2f, turned %.1f to %.1f): %s",
						v->name, acceleration, marker, p->death_push_position[0], p->death_push_position[1], p->death_push_position[2],
						p->death_push_direction[0], p->death_push_direction[1], p->death_push_direction[2], p->death_push_inner, p->death_push_cone,
						h2_cache_tag_name(cache, effect));
				}
			}
		}
	}
}

/* Halo 2's force model's values (h2_vehicle_physics.h) from its vehi tag
(at vehicle): the havok vehicle physics struct at 0x2AC (its anti-gravity
points at +0x3C, friction points at +0x44, 0x4C each, each at its marker),
its engine and gears (0x260..0x26C, 0x44 each), its driving, its physics
model's mass (its hlmt's 0x20, the phmo's 4), a rider's mass and its
driver's seat's turnover time */
/* a standing turret's aim limits: Halo 2's unit takes them from the aim screen
of the animation it aims with (bounds_112070, halo2-decomp src/unknown_10f1e0.cpp:
yaw -right frames x right yaw a frame .. left frames x left yaw a frame, pitch
the same way) - a turret's "combat:fixed:aim_still_up" in its model's (hlmt at
obje 0x34) animation graph (0x10): blend screens at 0x24, 0x1C each, their
bounds at 4; animations at 0x2C, 0x60 each, the name's string id at 0, the
screen's index at 0x12. Not its seat's yaw minimum and maximum, an actor's. */
H2_ALIGN static boolean h2v_turret_aim_limits(struct h2_cache *cache, unsigned long vehicle, struct h2vp_definition *p)
{
	struct h2_cache_tag model, graph;
	long hlmt = h2_cache_reference(cache, vehicle + 0x34), jmad, screen_count, animation_count, i;
	unsigned long screens, animations;

	if (hlmt == NONE || !h2_cache_tag_get(cache, hlmt, &model) || (jmad = h2_cache_reference(cache, model.address + 0x10)) == NONE ||
		!h2_cache_tag_get(cache, jmad, &graph) || !h2_cache_block(cache, graph.address + 0x24, 0x1C, &screen_count, &screens) ||
		!h2_cache_block(cache, graph.address + 0x2C, 0x60, &animation_count, &animations))
	{
		return FALSE;
	}
	for (i = 0; i < animation_count; i++)
	{
		unsigned char const *e = h2_cache_at(cache, animations + 0x60 * (unsigned long)i, 0x60);
		unsigned char const *b;
		char name[48];
		int screen;

		if (!e)
			continue;
		h2_cache_string(cache, read32(e), name, sizeof(name));
		screen = (signed char)e[0x12];
		if (strcmp(name, "combat:fixed:aim_still_up") || screen < 0 || screen >= screen_count ||
			!(b = h2_cache_at(cache, screens + 0x1C * (unsigned long)screen + 4, 0x18)))
		{
			continue;
		}
		p->yaw_limits[0] = -(float)read16(b + 8) * readf(b);
		p->yaw_limits[1] = (float)read16(b + 10) * readf(b + 4);
		p->pitch_limits[0] = -(float)read16(b + 20) * readf(b + 12);
		p->pitch_limits[1] = (float)read16(b + 22) * readf(b + 16);
		p->has_yaw_limits = 1;
		return TRUE;
	}
	return FALSE;
}

/* (device_machines.c) a vehicle definition's Halo 2 vehicle flags (0x1EC), 0
when it is not a Halo 2 vehicle - flag 20 "can trigger automatic opening
doors" opens a Halo 2 door */
unsigned long h2_vehicle_physics_definition_flags(long definition_index)
{
	struct h2vp_definition const *definition = h2_vehicle_physics_definition(definition_index);

	return definition ? definition->vehicle_flags : 0;
}

H2_ALIGN static void h2v_physics_read(struct h2_cache *cache, struct h2v_vehicle *v, unsigned long vehicle)
{
	struct h2vp_definition *p = &v->physics;
	unsigned long physics = vehicle + 0x2AC, block;
	long count, i;
	int q;

	memset(p, 0, sizeof(*p));
	p->magic = H2VP_MAGIC;
	p->version = H2VP_VERSION;
	p->vehicle_flags = h2_cache_read32(cache, vehicle + 0x1EC);
	p->type = (short)(h2_cache_read32(cache, vehicle + 0x1F0) & 0xFFFF);
	p->control = (short)(h2_cache_read32(cache, vehicle + 0x1F0) >> 16);
	p->specific_type = (short)(h2_cache_read32(cache, vehicle + 0x218) & 0xFFFF);
	for (q = 0; q < 4; q++)
	{
		p->speed[q] = h2w_read_real(cache, vehicle + 0x1F4 + 4 * q);
		p->slide[q] = h2w_read_real(cache, vehicle + 0x22C + 4 * q);
	}
	p->turn_angles[0] = h2w_read_real(cache, vehicle + 0x204);
	p->turn_angles[1] = h2w_read_real(cache, vehicle + 0x208);
	p->wheel_circumference = h2w_read_real(cache, vehicle + 0x20C);
	p->turn_rate = h2w_read_real(cache, vehicle + 0x210);
	p->overdampen_cusp = h2w_read_real(cache, vehicle + 0x250);
	p->overdampen_exponent = h2w_read_real(cache, vehicle + 0x254);
	p->turn_scale = h2w_read_real(cache, vehicle + 0x220);
	p->engine_momentum = h2w_read_real(cache, vehicle + 0x260);
	p->engine_maximum_angular_velocity = h2w_read_real(cache, vehicle + 0x264);
	p->flying_torque_scale = h2w_read_real(cache, vehicle + 0x270);
	p->seat_entrance_scale = h2w_read_real(cache, vehicle + 0x274);
	/* (its collision damage: obje 0x6C..0x8C, h2_collision_damage.c) */
	for (q = 0; q < 9; q++)
		p->collision[q] = h2w_read_real(cache, vehicle + 0x6C + 4 * q);
	p->has_collision = 1;
	if (getenv("HALO_H2_DEBUG"))
		h2v_physics_log("h2 vehicles: %s: collision damage: transfer %.2f, game acc %.2f..%.2f scale %.2f..%.2f, abs acc %.2f..%.2f scale %.2f..%.2f",
			v->name, p->collision[0], p->collision[1], p->collision[2], p->collision[3], p->collision[4],
			p->collision[5], p->collision[6], p->collision[7], p->collision[8]);
	p->seat_exit_scale = h2w_read_real(cache, vehicle + 0x278);
	p->boost_peak_power = h2w_read_real(cache, vehicle + 0x1D0);
	p->boost_rise_power = h2w_read_real(cache, vehicle + 0x1D4);
	p->boost_peak_time = h2w_read_real(cache, vehicle + 0x1D8);
	p->boost_fall_power = h2w_read_real(cache, vehicle + 0x1DC);
	p->boost_dead_time = h2w_read_real(cache, vehicle + 0x1E0);
	/* (its boost's sound: its attachments - 0x94, 0x18 each: the type a
	reference, its primary scale a string id after its marker - that are looping
	sounds scaled by "boost"; each the in and loop of its first track,
	made the level's sounds, h2w_sound) */
	if (h2_cache_block(cache, vehicle + 0x94, 0x18, &count, &block))
	{
		long sounds = 0;

		for (i = 0; i < count && sounds < 2; i++)
		{
			unsigned long at = block + 0x18 * (unsigned long)i;
			long type = h2_cache_reference(cache, at);
			struct h2_cache_tag a;
			char scale[64], folder[128], name[160];
			char const *tag_name, *last;

			int q;

			/* (its primary scale: the first string id set after its marker,
			as h2_effects.c reads it - its change color a short before it) */
			for (q = 0, scale[0] = 0; q < 3 && !scale[0]; q++)
			{
				unsigned long id = h2_cache_read32(cache, at + 0xC + 4 * (unsigned long)q);

				if (id)
					h2w_name(cache, id, scale, sizeof(scale));
			}
			if (type == NONE || !h2_cache_tag_get(cache, type, &a) || a.group_tag != 'lsnd' || strcmp(scale, "boost") ||
				!(tag_name = h2_cache_tag_name(cache, type)) || strncmp(tag_name, "sound\\", 6) || !(last = strrchr(tag_name, '\\')))
			{
				continue;
			}
			snprintf(folder, sizeof(folder), "%.*s\\track1\\", (int)(last - tag_name), tag_name);
			snprintf(name, sizeof(name), "%sin", folder);
			h2w_sound(cache, h2v_tag_named(cache, 'snd!', name));
			snprintf(name, sizeof(name), "%sloop", folder);
			if (h2w_sound(cache, h2v_tag_named(cache, 'snd!', name)) == NONE)
				continue;
			snprintf(p->boost_sounds[sounds++], sizeof(p->boost_sounds[0]), "%s", folder + 6);
		}
	}
	p->physics_flags = h2_cache_read32(cache, physics);
	p->ground_friction = h2w_read_real(cache, physics + 0x4);
	p->ground_depth = h2w_read_real(cache, physics + 0x8);
	p->ground_damp = h2w_read_real(cache, physics + 0xC);
	p->ground_moving_friction = h2w_read_real(cache, physics + 0x10);
	p->slope[0] = h2w_read_real(cache, physics + 0x14);
	p->slope[1] = h2w_read_real(cache, physics + 0x18);
	p->bank_lift = h2w_read_real(cache, physics + 0x2C);
	p->bank_reaction = h2w_read_real(cache, physics + 0x30);
	p->gravity_scale = h2w_read_real(cache, physics + 0x34);
	p->radius = h2w_read_real(cache, physics + 0x38);
	if (h2_cache_block(cache, vehicle + 0x268, 0x44, &count, &block))
	{
		for (i = 0; i < count && p->gear_count < H2VP_MAXIMUM_GEARS; i++)
		{
			unsigned long at = block + 0x44 * (unsigned long)i;
			struct h2vp_gear *g = &p->gears[p->gear_count++];

			for (q = 0; q < 6; q++)
			{
				g->loaded[q] = h2w_read_real(cache, at + 4 * q);
				g->cruising[q] = h2w_read_real(cache, at + 0x18 + 4 * q);
			}
			g->minimum_upshift_time = h2w_read_real(cache, at + 0x30);
			g->upshift_scale = h2w_read_real(cache, at + 0x34);
			g->ratio = h2w_read_real(cache, at + 0x38);
			g->minimum_downshift_time = h2w_read_real(cache, at + 0x3C);
			g->downshift_scale = h2w_read_real(cache, at + 0x40);
		}
	}
	if (h2_cache_block(cache, physics + 0x3C, 0x4C, &count, &block))
	{
		for (i = 0; i < count && p->antigrav_count < H2VP_MAXIMUM_POINTS; i++)
		{
			unsigned long at = block + 0x4C * (unsigned long)i;
			struct h2vp_antigrav_point *a = &p->antigrav[p->antigrav_count];
			char name[64];

			h2w_name(cache, h2_cache_read32(cache, at), name, sizeof(name));
			if (!h2v_marker_place(&v->model, name, a->position))
			{
				h2v_physics_log("h2 vehicles: %s: no marker '%s' for its anti-gravity point: left out", v->name, name);
				continue;
			}
			a->flags = h2_cache_read32(cache, at + 4);
			a->strength = h2w_read_real(cache, at + 0x8);
			a->height = h2w_read_real(cache, at + 0x10);
			a->damp = h2w_read_real(cache, at + 0x14);
			a->normal_k1 = h2w_read_real(cache, at + 0x18);
			a->normal_k0 = h2w_read_real(cache, at + 0x1C);
			a->radius = h2w_read_real(cache, at + 0x20);
			a->error = h2w_read_real(cache, at + 0x38);
			p->antigrav_count++;
		}
	}
	if (h2_cache_block(cache, physics + 0x44, 0x4C, &count, &block))
	{
		for (i = 0; i < count && p->friction_count < H2VP_MAXIMUM_POINTS; i++)
		{
			unsigned long at = block + 0x4C * (unsigned long)i;
			struct h2vp_friction_point *f = &p->friction[p->friction_count];
			char name[64];

			h2w_name(cache, h2_cache_read32(cache, at), name, sizeof(name));
			if (!h2v_marker_place(&v->model, name, f->position))
			{
				h2v_physics_log("h2 vehicles: %s: no marker '%s' for its friction point: left out", v->name, name);
				continue;
			}
			f->flags = h2_cache_read32(cache, at + 4);
			f->fraction = h2w_read_real(cache, at + 0x8);
			f->radius = h2w_read_real(cache, at + 0xC);
			f->moving_velocity_difference = h2w_read_real(cache, at + 0x18);
			f->e_brake_moving_friction = h2w_read_real(cache, at + 0x1C);
			f->e_brake_friction = h2w_read_real(cache, at + 0x20);
			f->e_brake_moving_velocity_difference = h2w_read_real(cache, at + 0x24);
			p->friction_count++;
		}
	}
	/* (a standing turret's aim: its aim screen's bounds) */
	if (strstr(h2v_kinds[v->kind].collection, "turret") && !getenv("HALO_H2_NO_TURRET_AIM_LIMITS") && h2v_turret_aim_limits(cache, vehicle, p))
	{
		h2v_physics_log("h2 vehicles: %s: its aim turns yaw %.1f..%.1f, pitch %.1f..%.1f degrees", v->name, p->yaw_limits[0] * 57.29578f,
			p->yaw_limits[1] * 57.29578f, p->pitch_limits[0] * 57.29578f, p->pitch_limits[1] * 57.29578f);
	}
	/* (its first seat's yaw limits - the seat's yaw minimum and maximum
	after its enter string: Halo 2 gives its actors these, not its aim -
	0x2973f0's cone, the anti-personnel turret's -5..5 -; kept for the
	vehicles with no aim screen of their own here) */
	else if (h2v_kinds[v->kind].own_graph && h2_cache_block(cache, vehicle + 0x1C8, 0xB0, &count, &block) && count > 0)
	{
		float minimum = h2w_read_real(cache, block + 0x88), maximum = h2w_read_real(cache, block + 0x8C);

		if (maximum > minimum && maximum - minimum < 6.2f && (minimum != 0.0f || maximum != 0.0f))
		{
			p->yaw_limits[0] = minimum;
			p->yaw_limits[1] = maximum;
			p->pitch_limits[0] = -1.5707964f;
			p->pitch_limits[1] = 1.5707964f;
			p->has_yaw_limits = 1;
			h2v_physics_log("h2 vehicles: %s: its seat yaws %.1f..%.1f degrees", v->name, minimum * 57.29578f, maximum * 57.29578f);
		}
	}
	/* (its driver's seat's turnover time: the seat labelled "..._d") */
	if (h2_cache_block(cache, vehicle + 0x1C8, 0xB0, &count, &block))
	{
		for (i = 0; i < count; i++)
		{
			char label[32];
			size_t length;

			if (h2_cache_string(cache, h2_cache_read32(cache, block + 0xB0 * (unsigned long)i + 4), label, sizeof(label)) &&
				(length = strlen(label)) > 2 && !strcmp(label + length - 2, "_d"))
			{
				p->turnover_time = h2w_read_real(cache, block + 0xB0 * (unsigned long)i + 0x20);
				break;
			}
		}
	}
	{
		long hlmt = h2v_tag_named(cache, 'hlmt', h2v_kinds[v->kind].pieces[0].render_model);
		long biped = h2v_tag_named(cache, 'bipd', "objects\\characters\\masterchief\\masterchief_mp");
		struct h2_cache_tag t;

		if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &t))
		{
			long phmo = h2_cache_reference(cache, t.address + 0x20);

			if (phmo != NONE && h2_cache_tag_get(cache, phmo, &t))
			{
				long bodies, b;
				unsigned long body;
				float heaviest = 0.0f;

				p->mass = h2w_read_real(cache, t.address + 4);
				/* (its polyhedra, 0x70, 0x120 each: name, half extents
				0x40, centre 0x50 - the hull's shapes, one each name, the
				largest: a destroyed body's copies are the same names) */
				{
					long polyhedron_count, q;
					unsigned long polyhedra;

					v->shape_count = 0;
					if (h2_cache_block(cache, t.address + 0x70, 0x120, &polyhedron_count, &polyhedra))
					{
						unsigned long names[12];

						for (q = 0; q < polyhedron_count; q++)
						{
							unsigned long at = polyhedra + 0x120 * (unsigned long)q, id = h2_cache_read32(cache, at);
							float box[6], volume;
							long w;
							int c;

							for (c = 0; c < 3; c++)
							{
								box[c] = h2w_read_real(cache, at + 0x50 + 4 * (unsigned long)c);
								box[3 + c] = h2w_read_real(cache, at + 0x40 + 4 * (unsigned long)c);
							}
							if (box[3] <= 0.0f || box[4] <= 0.0f || box[5] <= 0.0f || box[3] > 10.0f || box[4] > 10.0f || box[5] > 10.0f)
								continue;
							volume = box[3] * box[4] * box[5];
							for (w = 0; w < v->shape_count && names[w] != id; w++)
								;
							if (w < v->shape_count)
							{
								if (volume > v->shapes[w][3] * v->shapes[w][4] * v->shapes[w][5])
									memcpy(v->shapes[w], box, sizeof(box));
								continue;
							}
							if (v->shape_count >= 12)
								continue;
							names[v->shape_count] = id;
							memcpy(v->shapes[v->shape_count++], box, sizeof(box));
						}
					}
				}
				if (getenv("HALO_H2_PHMO_PROBE"))
				{
					unsigned long o;

					for (o = 0x28; o < 0xC0; o += 8)
					{
						long count = 0;
						unsigned long elements = 0;

						if (h2_cache_block(cache, t.address + o, 4, &count, &elements) && count > 0)
						{
							char text[4096] = "";
							long q;

							for (q = 0; q < (o == 0x70 ? 256 : 8); q++)
							{
								size_t n = strlen(text);

								snprintf(text + n, sizeof(text) - n, " %.3f", h2w_read_real(cache, elements + 4 * (unsigned long)q));
							}
							h2v_physics_log("phmo probe %s 0x%lx: %ld:%s", v->name, o, count, text);
						}
					}
				}
				if (h2_cache_block(cache, t.address + 0x38, 0xA0, &bodies, &body))
				{
					struct h2_cache_tag model_tag;
					long render = h2v_tag_named(cache, 'mode', h2v_kinds[v->kind].pieces[0].render_model), node_count = 0;
					unsigned long nodes = 0;
					float total = 0.0f, sum[3] = { 0.0f, 0.0f, 0.0f };

					if (render != NONE && h2_cache_tag_get(cache, render, &model_tag))
						h2_cache_block(cache, model_tag.address + 0x48, 0x60, &node_count, &nodes);
					for (b = 0; b < bodies; b++)
					{
						unsigned long at = body + 0xA0 * (unsigned long)b;
						short node = (short)(h2_cache_read32(cache, at) & 0xFFFF);
						float mass = h2w_read_real(cache, at + 0x40), local[3], place[3];
						int c;

						for (c = 0; c < 3; c++)
							local[c] = h2w_read_real(cache, at + 0x50 + 4 * (unsigned long)c);
						if (mass <= 0.0f)
							continue;
						if (node >= 0 && node < node_count)
						{
							/* (in its node's: the node's default, as its
							inverse - the model's 0x2C - gives it) */
							float inv[12];
							float d[3];
							int q;

							for (q = 0; q < 12; q++)
								inv[q] = h2w_read_real(cache, nodes + 0x60 * (unsigned long)node + 0x2C + 4 * (unsigned long)q);
							for (c = 0; c < 3; c++)
								d[c] = local[c] - inv[9 + c];
							for (c = 0; c < 3; c++)
								place[c] = inv[3 * c] * d[0] + inv[3 * c + 1] * d[1] + inv[3 * c + 2] * d[2];
						}
						else
						{
							for (c = 0; c < 3; c++)
								place[c] = local[c];
						}
						for (c = 0; c < 3; c++)
							sum[c] += place[c] * mass;
						total += mass;
					}
					if (total > 0.0f)
					{
						int c;

						for (c = 0; c < 3; c++)
							p->center_of_mass[c] = sum[c] / total;
						p->has_center_of_mass = 1;
					}
					h2v_hull_read(cache, v, p, t.address, nodes, node_count);
					for (b = 0; b < bodies; b++)
					{
						float mass = h2w_read_real(cache, body + 0xA0 * (unsigned long)b + 0x40);
						float linear = h2w_read_real(cache, body + 0xA0 * (unsigned long)b + 0x28);
						float angular = h2w_read_real(cache, body + 0xA0 * (unsigned long)b + 0x2C);

						if (mass > heaviest && linear >= 0.0f && linear < 10.0f && angular > -10.0f && angular < 10.0f)
						{
							heaviest = mass;
							p->linear_damping = linear;
							p->angular_damping = angular > 0.0f ? angular : 0.0f;
						}
					}
				}
			}
		}
		if (biped != NONE && h2_cache_tag_get(cache, biped, &t))
			p->rider_mass = h2w_read_real(cache, t.address + 0x274);
	}
	h2v_death_push_read(cache, v, p);
	p->weight_at_body = h2v_kinds[v->kind].own_body;
	h2v_physics_log("h2 vehicles: %s: force model type %d control %d kind %d, mass %.1f (damping %.3f, %.3f, centre %.3f %.3f %.3f), %ld friction points, %ld anti-gravity points, %ld gears, turnover %.2f s",
		v->name, p->type, p->control, p->specific_type, p->mass, p->linear_damping, p->angular_damping, p->center_of_mass[0], p->center_of_mass[1],
		p->center_of_mass[2], p->friction_count, p->antigrav_count, p->gear_count, p->turnover_time);
	if (getenv("HALO_H2_DEBUG"))
	{
		for (i = 0; i < p->friction_count; i++)
			h2v_physics_log("h2 vehicles: %s: friction point %ld at %.3f %.3f %.3f flags %lx fraction %.2f radius %.3f slip %.2f", v->name, i,
				p->friction[i].position[0], p->friction[i].position[1], p->friction[i].position[2], p->friction[i].flags,
				p->friction[i].fraction, p->friction[i].radius, p->friction[i].moving_velocity_difference);
		for (i = 0; i < p->antigrav_count; i++)
			h2v_physics_log("h2 vehicles: %s: anti-gravity point %ld at %.3f %.3f %.3f flags %lx strength %.2f height %.2f radius %.3f", v->name, i,
				p->antigrav[i].position[0], p->antigrav[i].position[1], p->antigrav[i].position[2], p->antigrav[i].flags,
				p->antigrav[i].strength, p->antigrav[i].height, p->antigrav[i].radius);
	}
}

/* a vehicle of a kind made; NULL if it cannot be */
/* ---------- private code: the vehicles' damage sections */

/* a name of the definition's (an effect's - 'e' then Halo 2's tag name -
or a debris piece's - 'd' then its name), its index; NONE if no room */
H2_ALIGN static short h2v_damage_name(struct h2vd_definition *d, struct h2v_vehicle *v, char kind, char const *name, long effect_tag)
{
	short i;

	for (i = 0; i < d->name_count; i++)
	{
		if (d->names[i][0] == kind && !strcmp(d->names[i] + 1, name))
			return i;
	}
	if (d->name_count >= H2VD_DEFINITION_NAMES)
		return NONE;
	snprintf(d->names[d->name_count], sizeof(d->names[0]), "%c%s", kind, name);
	v->damage_effects[d->name_count] = effect_tag;
	return d->name_count++;
}

/* an effect reference's (Halo 2's) name in the definition; NONE none */
H2_ALIGN static short h2v_damage_effect(struct h2_cache *cache, struct h2vd_definition *d, struct h2v_vehicle *v, unsigned long reference)
{
	long tag = h2_cache_reference(cache, reference);

	return tag != NONE ? h2v_damage_name(d, v, 'e', h2_cache_tag_name(cache, tag), tag) : NONE;
}

/* the garbage a Halo 2 effect throws (its events' parts, 0x14 and 0x18,
0x38 each, naming a crate or garbage object - type at 0xC, velocity bounds
0x14, cone angle 0x1C, angular velocity bounds 0x20 -, each at its location,
the effect's locations 0xC, a marker name each - "replace" its response's
effect marker, replace_marker): this engine's debris names (h2v_debris_names',
else one made of its render model's name, added to the debris made:
h2v.extra_debris), each part's own (the Warthog's hull_destroyed throws a
sailpanel from each side and its winch), in out; how many */
H2_ALIGN static long h2v_damage_debris(struct h2_cache *cache, long effect, struct h2vd_definition *d, struct h2v_vehicle *v,
	struct h2vd_definition_debris *out, long maximum, unsigned long replace_marker)
{
	struct h2_cache_tag t;
	long event_count, e, found = 0, location_count;
	unsigned long events, locations;

	if (effect == NONE || !h2_cache_tag_get(cache, effect, &t) || !h2_cache_block(cache, t.address + 0x14, 0x38, &event_count, &events))
		return 0;
	if (!h2_cache_block(cache, t.address + 0xC, 4, &location_count, &locations))
		location_count = 0;
	for (e = 0; e < event_count && found < maximum; e++)
	{
		long part_count, p;
		unsigned long parts;

		if (!h2_cache_block(cache, events + 0x38 * (unsigned long)e + 0x18, 0x38, &part_count, &parts))
			continue;
		for (p = 0; p < part_count && found < maximum; p++)
		{
			unsigned long o, part = parts + 0x38 * (unsigned long)p;

			/* (its type reference: a tag of the crate or garbage group) */
			for (o = 0; o + 8 <= 0x38 && found < maximum; o += 4)
			{
				long object = h2_cache_reference(cache, part + o);
				struct h2_cache_tag object_tag, model_tag;
				long hlmt, render, k;
				char const *render_name, *last;
				char name[32];
				short location;

				if (object == NONE || !h2_cache_tag_get(cache, object, &object_tag) ||
					(object_tag.group_tag != 'bloc' && object_tag.group_tag != 'garb') ||
					(hlmt = h2_cache_reference(cache, object_tag.address + 0x34)) == NONE || !h2_cache_tag_get(cache, hlmt, &model_tag) ||
					(render = h2_cache_reference(cache, model_tag.address)) == NONE)
				{
					continue;
				}
				render_name = h2_cache_tag_name(cache, render);
				name[0] = 0;
				for (k = 0; k < (long)(sizeof(h2v_debris_names) / sizeof(h2v_debris_names[0])); k++)
				{
					if (!strcmp(h2v_debris_names[k][1], render_name))
						snprintf(name, sizeof(name), "%s", h2v_debris_names[k][0]);
				}
				if (!name[0])
				{
					/* (its vehicle's folder and its own name: "banshee pod_garbage") */
					last = strrchr(render_name, '\\');
					snprintf(name, sizeof(name), "%s %s", v->name, last ? last + 1 : render_name);
					for (k = 0; k < h2v.extra_debris_count && strcmp(h2v.extra_debris[k][0], name); k++)
						;
					if (k == h2v.extra_debris_count && k < H2V_MAXIMUM_EXTRA_DEBRIS)
					{
						snprintf(h2v.extra_debris[k][0], sizeof(h2v.extra_debris[k][0]), "%s", name);
						snprintf(h2v.extra_debris[k][1], sizeof(h2v.extra_debris[k][1]), "%s", render_name);
						h2v.extra_debris_count++;
					}
				}
				out[found].name = h2v_damage_name(d, v, 'd', name, NONE);
				if (out[found].name == NONE)
					break;
				/* (its location's marker: the part's location, 4) */
				out[found].marker[0] = 0;
				location = h2w_read_short(cache, part + 4);
				if (location >= 0 && location < location_count)
				{
					char marker[32];

					h2w_name(cache, h2_cache_read32(cache, locations + 4 * (unsigned long)location), marker, sizeof(marker));
					if (!strcmp(marker, "replace"))
					{
						marker[0] = 0;
						if (replace_marker)
							h2w_name(cache, replace_marker, marker, sizeof(marker));
					}
					snprintf(out[found].marker, sizeof(out[found].marker), "%s", marker);
				}
				out[found].velocity[0] = h2w_read_real(cache, part + o + 8);
				out[found].velocity[1] = h2w_read_real(cache, part + o + 0xC);
				out[found].cone = h2w_read_real(cache, part + o + 0x10);
				out[found].angular_velocity[0] = h2w_read_real(cache, part + o + 0x14);
				out[found].angular_velocity[1] = h2w_read_real(cache, part + o + 0x18);
				found++;
				break;
			}
		}
	}
	return found;
}

/* a Halo 2 region's name (a string id) as the vehicle's model has it (its
'_' made ' ', merged as h2v_region_merge says); empty if the model has no
such region (past an object's eight) */
static struct model_regions_made const *h2v_damage_regions;  /* (h2v_damage_read's vehicle's model's) */
H2_ALIGN static void h2v_damage_region(struct h2_cache *cache, struct h2v_vehicle const *v, unsigned long id, char *out, long size)
{
	struct model_regions_made const *regions = h2v_damage_regions;
	char name[32];
	long i;

	out[0] = 0;
	if (!id || !h2_cache_string(cache, id, name, sizeof(name)) || !name[0])
		return;
	h2w_name(cache, id, name, sizeof(name));
	for (i = 0; h2v_region_merge[i]; i += 2)
	{
		if (!strcmp(name, h2v_region_merge[i]))
			snprintf(name, sizeof(name), "%s", h2v_region_merge[i + 1]);
	}
	for (i = 0; regions && i < regions->count; i++)
	{
		if (!strcmp(regions->names[i], name))
		{
			snprintf(out, size, "%s", name);
			return;
		}
	}
}

/* (h2v_vehicle_make) a damage info's (an hlmt's, at info) damage seats
(0xD8, 0x14 each: the seat's label, its direct damage scale, transfer
fall-off radius, maximum and minimum transfer scales), kept for the seats
of those labels; child: its child object's */
H2_ALIGN static void h2v_damage_seats_read(struct h2_cache *cache, struct h2v_vehicle *v, unsigned long info, boolean child)
{
	long seat_count, i;
	unsigned long seats;

	if (!h2_cache_block(cache, info + 0xD8, 0x14, &seat_count, &seats))
		return;
	for (i = 0; i < seat_count && v->damage_seat_count < (long)(sizeof(v->damage_seats) / sizeof(v->damage_seats[0])); i++)
	{
		unsigned long at = seats + 0x14 * (unsigned long)i;
		long n = v->damage_seat_count, d;
		int q;

		if (!h2_cache_string(cache, h2_cache_read32(cache, at), v->damage_seats[n].label, sizeof(v->damage_seats[n].label)))
			continue;
		for (d = 0; d < n && strcmp(v->damage_seats[d].label, v->damage_seats[n].label); d++)
			;
		if (d < n)
			continue;
		for (q = 0; q < 4; q++)
			v->damage_seats[n].scales[q] = h2w_read_real(cache, at + 4 + 4 * (unsigned long)q);
		v->damage_seats[n].child = child;
		v->damage_seat_count++;
	}
}

/* (h2v_vehicle_make) the vehicle's pieces past its first - Halo 2's child
objects, its turret -: their hlmts' damage seats, and the nodes of its
rig only they are renamed to (the Scorpion's turret rotator, turret,
aiming gun and cannon; the Warthog's gun mount base, chain, gun and
barrels), h2vp_definition's child_nodes */
H2_ALIGN static void h2v_child_parts_read(struct h2_cache *cache, struct h2v_vehicle *v)
{
	struct h2v_kind const *k = &h2v_kinds[v->kind];
	char first[64][32];
	long first_count = 0, p;

	v->physics.child_node_count = 0;
	if (k->own_graph && !getenv("HALO_H2_NO_TURRET_COLLISION"))
	{
		/* (its own rig: Halo 2's nodes as they are - the nodes of the model of
		each piece past the first, not the first's, are its child object's) */
		for (p = 1; p < H2V_MAXIMUM_PIECES && k->pieces[p].render_model; p++)
		{
			long hlmt = h2v_piece_hlmt(cache, k->pieces[p].render_model), render = h2v_tag_named(cache, 'mode', k->pieces[p].render_model);
			long count, node_count, n, e;
			struct h2_cache_tag t, first_model;
			unsigned long nodes, first_nodes, info;
			long first_render = h2v_tag_named(cache, 'mode', k->pieces[0].render_model), first_count = 0;

			if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &t) && h2_cache_block(cache, t.address + 0x60, 0xF8, &count, &info) && count > 0)
				h2v_damage_seats_read(cache, v, info, TRUE);
			if (render == NONE || !h2_cache_tag_get(cache, render, &t) || !h2_cache_block(cache, t.address + 0x48, 0x60, &node_count, &nodes))
				continue;
			if (first_render != NONE && h2_cache_tag_get(cache, first_render, &first_model))
				h2_cache_block(cache, first_model.address + 0x48, 0x60, &first_count, &first_nodes);
			for (n = 0; n < node_count; n++)
			{
				char name[64], other[64];
				long d;

				h2w_name(cache, h2_cache_read32(cache, nodes + 0x60 * (unsigned long)n), name, sizeof(name));
				for (e = 0; e < first_count; e++)
				{
					h2w_name(cache, h2_cache_read32(cache, first_nodes + 0x60 * (unsigned long)e), other, sizeof(other));
					if (!strcmp(name, other))
						break;
				}
				if (e < first_count)
					continue;
				for (d = 0; d < v->physics.child_node_count && strcmp(v->physics.child_nodes[d], name); d++)
					;
				if (d == v->physics.child_node_count && d < H2VP_MAXIMUM_CHILD_NODES)
				{
					snprintf(v->physics.child_nodes[d], sizeof(v->physics.child_nodes[d]), "%s", name);
					v->physics.child_node_count++;
				}
			}
		}
	}
	if (k->own_graph || !k->names)
	{
		long d;

		if (getenv("HALO_H2_DEBUG"))
		{
			for (d = 0; d < v->physics.child_node_count; d++)
				platform_log("h2 vehicles: %s: node '%s' its child object's (damage info flags %lx)", v->name, v->physics.child_nodes[d],
					v->physics.damage_info_flags);
		}
		return;
	}
	for (p = 0; p < H2V_MAXIMUM_PIECES && k->pieces[p].render_model; p++)
	{
		long render = h2v_tag_named(cache, 'mode', k->pieces[p].render_model), node_count, n;
		long hlmt = p > 0 ? h2v_tag_named(cache, 'hlmt', k->pieces[p].render_model) : NONE, count;
		struct h2_cache_tag t;
		unsigned long nodes, info;

		if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &t) && h2_cache_block(cache, t.address + 0x60, 0xF8, &count, &info) && count > 0)
			h2v_damage_seats_read(cache, v, info, TRUE);
		if (render == NONE || !h2_cache_tag_get(cache, render, &t) || !h2_cache_block(cache, t.address + 0x48, 0x60, &node_count, &nodes))
			continue;
		for (n = 0; n < node_count; n++)
		{
			char name[64];
			long q, d;

			h2w_name(cache, h2_cache_read32(cache, nodes + 0x60 * (unsigned long)n), name, sizeof(name));
			for (q = 0; k->names[q].h2 && strcmp(k->names[q].h2, name); q++)
				;
			if (!k->names[q].h2)
				continue;
			if (p == 0)
			{
				if (first_count < 64)
					snprintf(first[first_count++], sizeof(first[0]), "%s", k->names[q].ce);
				continue;
			}
			for (d = 0; d < first_count && strcmp(first[d], k->names[q].ce); d++)
				;
			if (d < first_count)
				continue;
			for (d = 0; d < v->physics.child_node_count && strcmp(v->physics.child_nodes[d], k->names[q].ce); d++)
				;
			if (d == v->physics.child_node_count && d < H2VP_MAXIMUM_CHILD_NODES)
			{
				snprintf(v->physics.child_nodes[d], sizeof(v->physics.child_nodes[d]), "%s", k->names[q].ce);
				v->physics.child_node_count++;
			}
		}
	}
	if (getenv("HALO_H2_DEBUG") && v->physics.child_node_count > 0)
	{
		long d;

		for (d = 0; d < v->physics.child_node_count; d++)
			platform_log("h2 vehicles: %s: node '%s' its child object's (damage info flags %lx)", v->name, v->physics.child_nodes[d],
				v->physics.damage_info_flags);
	}
}

/* (h2v_vehicle_make, its model made) Halo 2's damage of the vehicle: its
hlmt's damage info's (0x60, 0xF8 each) indirect damage section (8),
sections (0xBC, 0x38 each: name, flags 4, vitality 8, instant responses
0xC, 0x50 each: type 0, flags 4, threshold 8, transition effect 0xC,
transition damage effect 0x14, region 0x1C, new state 0x20, delay 0x2C,
delay effect 0x30, ejecting seat 0x40, total damage threshold 0x4C) and
its default variant's (0x50, 0x38 each; regions 0x14, 0x14 each: name,
permutations 8, 0x20 each: name, states 0xC, 0x18 each: permutation name,
state 6, looping effect 8) first permutations' damage states */
H2_ALIGN static void h2v_damage_read(struct build *build, struct h2v_vehicle *v, long hlmt)
{
	struct h2_cache *cache = build->cache;
	struct h2_cache_tag t;
	struct h2vd_definition *d;
	long count, section_count, s, variant_count, region_count, r, chosen = 0;
	unsigned long info, sections, variants, regions;

	if (!h2_cache_tag_get(cache, hlmt, &t) || !h2_cache_block(cache, t.address + 0x60, 0xF8, &count, &info) || count < 1 ||
		!h2_cache_block(cache, info + 0xBC, 0x38, &section_count, &sections) || !(d = calloc(1, sizeof(*d))))
	{
		return;
	}
	for (s = 0; s < H2VD_DEFINITION_NAMES; s++)
		v->damage_effects[s] = NONE;
	h2v_damage_regions = v->model.model != NONE ? build->models[v->model.model].regions : NULL;
	snprintf(d->vehicle, sizeof(d->vehicle), "h2\\vehicles\\%s", v->name);
	d->vitality = h2w_read_real(cache, info + 0x28);
	d->indirect_section = (short)h2w_read_short(cache, info + 8);
	for (s = 0; s < section_count && d->section_count < H2VD_DEFINITION_SECTIONS; s++)
	{
		struct h2vd_definition_section *section = &d->sections[d->section_count++];
		unsigned long at = sections + 0x38 * (unsigned long)s, responses;
		long response_count, k;

		if (!h2_cache_string(cache, h2_cache_read32(cache, at), section->name, sizeof(section->name)))
			section->name[0] = 0;
		section->flags = h2_cache_read32(cache, at + 4);
		section->vitality = h2w_read_real(cache, at + 8);
		if (!h2_cache_block(cache, at + 0xC, 0x50, &response_count, &responses))
			response_count = 0;
		for (k = 0; k < response_count && section->response_count < H2VD_DEFINITION_RESPONSES; k++)
		{
			struct h2vd_definition_response *response = &section->responses[section->response_count++];
			unsigned long a = responses + 0x50 * (unsigned long)k;
			long q, n;

			response->type = (short)h2w_read_short(cache, a);
			response->flags = h2_cache_read32(cache, a + 4);
			response->threshold = h2w_read_real(cache, a + 8);
			response->effect = h2v_damage_effect(cache, d, v, a + 0xC);
			response->damage_effect = NONE;
			{
				long damage = h2_cache_reference(cache, a + 0x14);

				if (damage != NONE)
				{
					long b;
					char const *last = strrchr(h2_cache_tag_name(cache, damage), '\\');

					response->damage_effect = h2v_damage_name(d, v, 'j', last ? last + 1 : h2_cache_tag_name(cache, damage), NONE);
					for (b = 0; b < h2v.blast_count && h2v.blasts[b] != damage; b++)
						;
					if (b == h2v.blast_count && b < 16)
						h2v.blasts[h2v.blast_count++] = damage;
				}
			}
			h2v_damage_region(cache, v, h2_cache_read32(cache, a + 0x1C), response->region, sizeof(response->region));
			response->state = (short)h2w_read_short(cache, a + 0x20);
			response->delay = h2w_read_real(cache, a + 0x2C);
			response->delay_effect = h2v_damage_effect(cache, d, v, a + 0x30);
			if (!h2_cache_string(cache, h2_cache_read32(cache, a + 0x40), response->eject, sizeof(response->eject)))
				response->eject[0] = 0;
			response->total_threshold = h2w_read_real(cache, a + 0x4C);
			for (q = 0; q < H2VD_DEFINITION_DEBRIS; q++)
				response->debris[q].name = NONE;
			/* (its transition effect's marker, 0x24: its location "replace") */
			response->effect_marker[0] = 0;
			if (h2_cache_read32(cache, a + 0x24))
				h2w_name(cache, h2_cache_read32(cache, a + 0x24), response->effect_marker, sizeof(response->effect_marker));
			n = h2v_damage_debris(cache, h2_cache_reference(cache, a + 0xC), d, v, response->debris, H2VD_DEFINITION_DEBRIS,
				h2_cache_read32(cache, a + 0x24));
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 vehicles: %s damage: section %s (%.3f, flags %lx) response type %d at %.2f (total %.2f): %s -> state %d, "
					"effect %s, delay %.2f, eject '%s', flags %lx, %ld pieces", v->name, section->name, section->vitality, section->flags,
					(int)response->type, response->threshold, response->total_threshold, response->region[0] ? response->region : "(none)",
					(int)response->state, response->effect != NONE ? d->names[response->effect] + 1 : "(none)", response->delay,
					response->eject, response->flags, n);
			if (getenv("HALO_H2_DEBUG"))
			{
				for (q = 0; q < n; q++)
					platform_log("h2 vehicles: %s damage:   piece %s at '%s' (effect at '%s'), %.2f..%.2f wu/s in %.1f deg, spin %.0f..%.0f deg/s",
						v->name, d->names[response->debris[q].name] + 1, response->debris[q].marker, response->effect_marker,
						response->debris[q].velocity[0], response->debris[q].velocity[1], response->debris[q].cone * 57.29578f,
						response->debris[q].angular_velocity[0] * 57.29578f, response->debris[q].angular_velocity[1] * 57.29578f);
			}
		}
	}
	/* (its default variant - else its first: its regions' damage states) */
	if (h2_cache_block(cache, t.address + 0x50, 0x38, &variant_count, &variants) && variant_count > 0)
	{
		long i;

		for (i = 0; i < variant_count; i++)
		{
			char name[32];

			if (h2_cache_string(cache, h2_cache_read32(cache, variants + 0x38 * (unsigned long)i), name, sizeof(name)) && !strcmp(name, "default"))
			{
				chosen = i;
				break;
			}
		}
		if (h2_cache_block(cache, variants + 0x38 * (unsigned long)chosen + 0x14, 0x14, &region_count, &regions))
		{
			for (r = 0; r < region_count && d->region_count < H2VD_DEFINITION_REGIONS; r++)
			{
				struct h2vd_definition_region *region = &d->regions[d->region_count];
				long permutation_count, state_count, k;
				unsigned long permutations, states;

				h2v_damage_region(cache, v, h2_cache_read32(cache, regions + 0x14 * (unsigned long)r), region->name, sizeof(region->name));
				if (getenv("HALO_H2_DEBUG"))
				{
					char raw[32] = "";
					long pc = 0, sc = 0;
					unsigned long pp = 0, ss = 0;

					h2_cache_string(cache, h2_cache_read32(cache, regions + 0x14 * (unsigned long)r), raw, sizeof(raw));
					h2_cache_block(cache, regions + 0x14 * (unsigned long)r + 8, 0x20, &pc, &pp);
					if (pc > 0)
						h2_cache_block(cache, pp + 0xC, 0x18, &sc, &ss);
					platform_log("h2 vehicles: %s damage: variant region '%s' (ours '%s'): %ld permutations, %ld states", v->name, raw, region->name, pc, sc);
				}
				if (!region->name[0] || !h2_cache_block(cache, regions + 0x14 * (unsigned long)r + 8, 0x20, &permutation_count, &permutations) ||
					permutation_count < 1 || !h2_cache_block(cache, permutations + 0xC, 0x18, &state_count, &states))
				{
					continue;
				}
				for (k = 0; k < H2VD_STATES; k++)
					region->looping_effects[k] = NONE;
				for (k = 0; k < state_count; k++)
				{
					unsigned long a = states + 0x18 * (unsigned long)k;
					short state = (short)h2w_read_short(cache, a + 6);

					char permutation[24];

					if (state < 0 || state >= H2VD_STATES)
						continue;
					/* (a state's first permutation: its "blur" ones - a moving
					tread's - are not made, h2w_model_add_regions) */
					h2w_name(cache, h2_cache_read32(cache, a), permutation, sizeof(permutation));
					if (strstr(permutation, "blur") || region->permutations[state][0])
						continue;
					snprintf(region->permutations[state], sizeof(region->permutations[state]), "%s", permutation);
					region->looping_effects[state] = h2v_damage_effect(cache, d, v, a + 8);
					/* (its looping effect marker name, 0x10) */
					region->looping_markers[state][0] = 0;
					if (h2_cache_read32(cache, a + 0x10))
						h2w_name(cache, h2_cache_read32(cache, a + 0x10), region->looping_markers[state], sizeof(region->looping_markers[state]));
					if (getenv("HALO_H2_DEBUG"))
						platform_log("h2 vehicles: %s damage: region %s state %d: permutation '%s', looping %s at '%s'", v->name, region->name, (int)state,
							region->permutations[state], region->looping_effects[state] != NONE ? d->names[region->looping_effects[state]] + 1 : "(none)",
							region->looping_markers[state]);
				}
				d->region_count++;
			}
		}
	}
	v->damage = d;
	platform_log("h2 vehicles: %s: its damage Halo 2's: vitality %.0f, %d sections, %d regions with damage states, %d effects and pieces",
		v->name, d->vitality, (int)d->section_count, (int)d->region_count, (int)d->name_count);
}

/* a Halo 2 vehicle's (its vehi at address) values h2v_root_values_write
writes: driving, slide, flip back, collision transfer and acceleration
scales, its motion sensor blip's size */
H2_ALIGN static void h2v_vehicle_values_read(struct h2_cache *cache, unsigned long address, struct h2v_vehicle *v)
{
	int q;

	for (q = 0; q < 9; q++)
		v->driving[q] = h2w_read_real(cache, address + 0x1F4 + 4 * q);
	v->has_driving = v->driving[0] > 0.0f;
	for (q = 0; q < 4; q++)
		v->slide[q] = h2w_read_real(cache, address + 0x22C + 4 * q);
	v->flip[0] = h2w_read_real(cache, address + 0x23C);
	v->flip[1] = h2w_read_real(cache, address + 0x240);
	v->collision_transfer_scale = h2w_read_real(cache, address + 0x6C);
	v->acceleration_scale = h2w_read_real(cache, address + 0x14);
	/* (its motion sensor blip's size) */
	{
		unsigned char const *size = h2_cache_at(cache, address + 0x194, 2);

		v->blip_size = -1;
		if (size && size[0] <= 2 && size[1] == 0)
			v->blip_size = size[0];
	}
}

/* the Halo 2 vehicle tag kind's vehicle is made from (NONE: none) */
H2_ALIGN static long h2v_source_tag(struct h2_cache *cache, long kind)
{
	struct h2v_kind const *k = &h2v_kinds[kind];
	long h2_vehicle = h2v_tag_named(cache, 'vehi', k->pieces[0].render_model);

	if (h2_vehicle == NONE)
	{
		char name[160];

		snprintf(name, sizeof(name), "%s_no_flare", k->pieces[0].render_model);
		h2_vehicle = h2v_tag_named(cache, 'vehi', name);
	}
	return h2_vehicle;
}

/* a Halo 2 seat's (at: its 0xB0 bytes) first camera track's control
points (0x74: its tracks, 8 bytes each, a 'trak' reference; the track's
control points at 4, 0x1C each: a position and an orientation), at most 16;
count 0 none */
H2_ALIGN static void h2v_seat_track_read(struct h2_cache *cache, unsigned long at, long *count, float points[16][7])
{
	long tracks, track, n = 0;
	unsigned long track_block, point_block;
	struct h2_cache_tag track_tag;
	long point_count;

	*count = 0;
	if (!h2_cache_block(cache, at + 0x74, 8, &tracks, &track_block) || tracks < 1 ||
		(track = h2_cache_reference(cache, track_block)) == NONE || !h2_cache_tag_get(cache, track, &track_tag) ||
		!h2_cache_block(cache, track_tag.address + 4, 0x1C, &point_count, &point_block))
	{
		return;
	}
	for (n = 0; n < point_count && n < 16; n++)
	{
		int q;

		for (q = 0; q < 7; q++)
			points[n][q] = h2w_read_real(cache, point_block + 0x1C * (unsigned long)n + 4 * (unsigned long)q);
	}
	*count = n;
}

/* a Halo 2 seat (at: its 0xB0 bytes) read: its label at 4, marker 8,
flags 0, entry radius 0x98, camera marker 0x60, pitch range 0x6C..0x70, its
camera track (0x74), maximum relative velocity 0xA4; FALSE with no label */
H2_ALIGN static boolean h2v_seat_read(struct h2_cache *cache, unsigned long at, struct h2v_seat *seat)
{
	char none[4];

	memset(seat, 0, sizeof(*seat));
	seat->track_slot = NONE;
	if (!h2_cache_string(cache, h2_cache_read32(cache, at + 4), seat->label, sizeof(seat->label)))
		return FALSE;
	if (!h2_cache_string(cache, h2_cache_read32(cache, at + 8), seat->marker, sizeof(seat->marker)))
		seat->marker[0] = 0;
	seat->flags = h2_cache_read32(cache, at);
	seat->entry_radius = h2w_read_real(cache, at + 0x98);
	h2w_name(cache, h2_cache_read32(cache, at + 0x60), seat->camera_marker, sizeof(seat->camera_marker));
	/* (none - the Warthog's and Ghost's driver's -: the vehicle's origin, as
	this engine's empty seat camera marker) */
	if (!h2_cache_string(cache, h2_cache_read32(cache, at + 0x60), none, sizeof(none)) || !none[0])
		seat->camera_marker[0] = 0;
	seat->camera_pitch[0] = h2w_read_real(cache, at + 0x6C);
	seat->camera_pitch[1] = h2w_read_real(cache, at + 0x70);
	h2v_seat_track_read(cache, at, &seat->camera_point_count, seat->camera_points);
	seat->maximum_relative_velocity = h2w_read_real(cache, at + 0xA4);
	return TRUE;
}

H2_ALIGN static struct h2v_vehicle *h2v_vehicle_make(struct build *build, long kind)
{
	struct h2_cache *cache = build->cache;
	struct h2v_kind const *k = &h2v_kinds[kind];
	struct h2v_vehicle *v = calloc(1, sizeof(*v));
	long p, piece_count = 0, pieces_in_map = 0;
	long h2_vehicle;

	if (!v)
		return NULL;
	v->kind = kind;
	v->base_vehicle = NONE;
	v->blip_size = -1;
	v->gun_sound = NONE;
	v->gun_damage_tag = NONE;
	v->gun_detonation_tag = NONE;
	v->gun2_impact_tag = NONE;
	v->gun2_projectile = NONE;
	snprintf(v->name, sizeof(v->name), "%s", k->collection);
	h2w_model_begin(&v->model);
	v->model.levels = TRUE;
	/* (its regions kept, each with its damage states - h2_vehicle_wrecks.c
	sets them - its turret drawn in each of its hull's) */
	v->model.regioned = !getenv("HALO_H2_NO_VEHICLE_REGIONS");
	v->model.merge = h2v_region_merge;
	for (p = 0; p < H2V_MAXIMUM_PIECES && k->pieces[p].render_model; p++)
	{
		long tag = h2v_tag_named(cache, 'mode', k->pieces[p].render_model);
		float at[3];

		if (tag != NONE)
			pieces_in_map++;
		float const *offset = k->pieces[p].offset;

		v->model.piece_region = p ? "hull" : NULL;
		/* (at a marker of the first piece's: Halo 2's child object's place) */
		if (k->pieces[p].marker && p > 0)
		{
			if (!h2v_marker_place(&v->model, k->pieces[p].marker, at))
			{
				platform_log("h2 vehicles: %s: no marker '%s' for its %s: left out", k->collection, k->pieces[p].marker, k->pieces[p].render_model);
				continue;
			}
			offset = at;
		}
		if (tag != NONE)
		{
			/* (the detail asked for, else the nearest the model has: the
			turret has fewer) */
			static short const details[] = { -1, 3, 4, 5, 1, 0 };
			int d;

			for (d = 0; d < (int)(sizeof(details) / sizeof(details[0])); d++)
			{
				short detail = details[d] < 0 ? h2w_detail("HALO_H2_VEHICLE_DETAIL", H2W_VEHICLE_DETAIL_DEFAULT) : details[d];

				if (h2w_model_add_renamed(build, &v->model, tag, NULL, detail, FALSE, k->own_graph ? NULL : k->names, offset))
				{
					piece_count++;
					break;
				}
			}
		}
	}
	/* (boarding, h2_caps_boarding: each boarding marker - "warthog b d
	enter"... - again as "<name> board", its turn Halo 2's own: the
	boarding seats' markers, h2v_seats_write. h2w_model_write conjugates a
	marker's turn as it does a node's; a boarder at the conjugate stood in
	the floor of the Warthog's cab, its legs under the chassis, at Halo 2's
	on the side step leaning in over the driver - the screenshots,
	HALO_H2_BOARD_MARKER=as-is for the other. The other markers keep the
	conjugate: a seat's are unturned, and the weapons' were never seen to
	need it - worth a look) */
	if (h2_caps_boarding())
	{
		long i, count = v->model.marker_count;
		boolean as_is = getenv("HALO_H2_BOARD_MARKER") && !strcmp(getenv("HALO_H2_BOARD_MARKER"), "as-is");

		for (i = 0; i < count && v->model.marker_count < H2W_MAXIMUM_MARKERS; i++)
		{
			struct h2w_marker *mk = &v->model.markers[i];
			size_t length = strlen(mk->name);

			if (length > 12 && !strcmp(mk->name + length - 6, " enter") && (strstr(mk->name, " b ") != NULL))
			{
				struct h2w_marker *copy = &v->model.markers[v->model.marker_count++];
				int c;

				*copy = *mk;
				snprintf(copy->name, sizeof(copy->name), "%.24s board", mk->name);
				for (c = 0; c < 3 && !as_is; c++)
					copy->rotation[c] = -copy->rotation[c];
			}
		}
	}
	/* (the turret: its nodes' default turns this engine's - none - in place
	of Halo 2's: the stand-in's aim turns them from none, and Halo 2's
	turret's gun lies turned in its default; markers moved with them) */
	if (piece_count && k->own_graph)
		h2v_native_begin(v);
	if (piece_count && h2v_kinds[kind].unturned && !getenv("HALO_H2_TURRET_AS_IS"))
		h2v_model_unturn(&v->model);
	if (piece_count && k->own_graph)
		h2v_native_finish(v);
	if (!piece_count || (!k->own_graph && !h2v_model_reorder(&v->model, k->skeleton, k->skeleton_count, k->own_body || k->places_from_model)) ||
		!h2w_model_finish(build, &v->model))
	{
		/* (a vehicle the level does not have - no Warthog on 01a - is not a
		failure: said so, the sweeps' logs then showing the real ones) */
		if (!pieces_in_map)
			platform_log("h2 vehicles: %s: not in this map (none of its render models)", k->collection);
		else
			platform_log("h2 vehicles: %s not made (its models could not be read or are not its stand-in's rig): the stand-in stays", k->collection);
		if (getenv("HALO_H2_DEBUG_MARKERS"))
		{
			long i;

			for (i = 0; i < h2_cache_tag_count(cache); i++)
			{
				struct h2_cache_tag c;

				if (h2_cache_tag_get(cache, i, &c) && strstr(h2_cache_tag_name(cache, i), k->collection))
				{
					long count, n;
					unsigned long nodes;

					platform_log("h2 vehicles: %s: tag %.4s %s", k->collection, (char const *)&c.group_tag, h2_cache_tag_name(cache, i));
					if (c.group_tag == 'mode' && h2_cache_block(cache, c.address + 0x48, 0x60, &count, &nodes))
					{
						for (n = 0; n < count; n++)
						{
							char name[32];
							unsigned char const *at = h2_cache_at(cache, nodes + 0x60 * n, 0x60);

							if (!at)
								continue;
							h2w_name(cache, read32(at), name, sizeof(name));
							platform_log("h2 vehicles: %s: node %ld '%s' parent %d at %.3f %.3f %.3f", k->collection, n, name, (int)(short)read16(at + 4),
								readf(at + 0xC), readf(at + 0x10), readf(at + 0x14));
						}
					}
				}
			}
		}
		free(v);
		return NULL;
	}
	/* Halo 2's driving (its vehicle's 0x1F4..0x214: maximum forward and
	reverse speed, acceleration, deceleration, maximum left and right turn,
	wheel circumference, turn rate, blur speed - this engine's same nine,
	a second's where its are a tick's) */
	h2_vehicle = h2v_tag_named(cache, 'vehi', k->pieces[0].render_model);
	/* (00a's gravity thrones: "gravity_throne_no_flare" its vehicle) */
	if (h2_vehicle == NONE)
	{
		char name[160];

		snprintf(name, sizeof(name), "%s_no_flare", k->pieces[0].render_model);
		h2_vehicle = h2v_tag_named(cache, 'vehi', name);
	}
	if (h2_vehicle != NONE)
	{
		struct h2_cache_tag t;
		int q;

		if (h2_cache_tag_get(cache, h2_vehicle, &t))
		{
			long count, i;
			unsigned long seats;

			h2v_vehicle_values_read(cache, t.address, v);
			/* (its seats at 0x1C8, 0xB0 each: the label at 4, the maximum
			relative velocity at 0xA4) */
			if (h2_cache_block(cache, t.address + 0x1C8, 0xB0, &count, &seats))
			{
				for (i = 0; i < count && v->seat_count < 16; i++)
				{
					unsigned long at = seats + 0xB0 * (unsigned long)i;

					if (h2v_seat_read(cache, at, &v->seats[v->seat_count]))
					{
						if (getenv("HALO_H2_DEBUG"))
							platform_log("h2 vehicles: %s seat %s: flags %lx, entry radius %.2f, maximum relative velocity %.2f", v->name,
								v->seats[v->seat_count].label, v->seats[v->seat_count].flags, v->seats[v->seat_count].entry_radius,
								h2w_read_real(cache, at + 0xA4));
						v->seats[v->seat_count++].maximum_relative_velocity = h2w_read_real(cache, at + 0xA4);
					}
				}
			}
			/* (Halo 2's force model's values) */
			h2v_physics_read(cache, v, t.address);
			/* (its own collision model and its gunner's seat's camera: the
			seat's camera marker at 0x60, pitch range 0x6C..0x70, camera
			tracks at 0x74 - references, 8 bytes each -, a track's control
			points at 4, 0x1C each: position and orientation) */
			v->h2_collision = v->h2_render_model = NONE;
			if (k->own_collision)
			{
				long hlmt = h2v_tag_named(cache, 'hlmt', k->pieces[0].render_model);
				struct h2_cache_tag model_tag;

				if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &model_tag))
				{
					v->h2_render_model = h2_cache_reference(cache, model_tag.address + 0x0);
					v->h2_collision = h2_cache_reference(cache, model_tag.address + 0x8);
				}
				if (h2_cache_block(cache, t.address + 0x1C8, 0xB0, &count, &seats) && count > 0)
				{
					long tracks;
					unsigned long track_block;

					h2w_name(cache, h2_cache_read32(cache, seats + 0x60), v->camera_marker, sizeof(v->camera_marker));
					v->camera_pitch[0] = h2w_read_real(cache, seats + 0x6C);
					v->camera_pitch[1] = h2w_read_real(cache, seats + 0x70);
					if (h2_cache_block(cache, seats + 0x74, 8, &tracks, &track_block) && tracks > 0)
					{
						long track = h2_cache_reference(cache, track_block);
						struct h2_cache_tag track_tag;
						long points;
						unsigned long point_block;

						if (track != NONE && h2_cache_tag_get(cache, track, &track_tag) &&
							h2_cache_block(cache, track_tag.address + 4, 0x1C, &points, &point_block))
						{
							long n;
							int q;

							for (n = 0; n < points && n < 16; n++)
							{
								for (q = 0; q < 7; q++)
									v->camera_points[n][q] = h2w_read_real(cache, point_block + 0x1C * (unsigned long)n + 4 * (unsigned long)q);
							}
							v->camera_point_count = n;
						}
					}
				}
				platform_log("h2 vehicles: %s: its own collision model %s, its seat's camera '%s' (pitch %.1f..%.1f), %ld track points", v->name,
					v->h2_collision != NONE ? "read" : "missing", v->camera_marker, v->camera_pitch[0] * 57.29578f, v->camera_pitch[1] * 57.29578f,
					v->camera_point_count);
			}
		}
	}
	/* (every other seat's camera track Halo 2's too - a tag each,
	h2v_seat_track_tag -: the own-collision kinds' first seat has its own
	above; HALO_H2_STANDIN_SEAT_CAMERAS=1 the stand-in's tracks, as before) */
	if (!getenv("HALO_H2_STANDIN_SEAT_CAMERAS"))
	{
		long i, p;

		/* (its child objects' vehicles' seats - a piece's render model's
		name is its vehicle's -, those of labels its own lacks) */
		for (p = 1; p < H2V_MAXIMUM_PIECES && k->pieces[p].render_model && v->child_seat_count < 4; p++)
		{
			long child = h2v_tag_named(cache, 'vehi', k->pieces[p].render_model), count, q;
			struct h2_cache_tag ct;
			unsigned long child_seats;

			if (child == NONE || !h2_cache_tag_get(cache, child, &ct) || !h2_cache_block(cache, ct.address + 0x1C8, 0xB0, &count, &child_seats))
				continue;
			for (i = 0; i < count && v->child_seat_count < 4; i++)
			{
				struct h2v_seat *seat = &v->child_seats[v->child_seat_count];

				if (!h2v_seat_read(cache, child_seats + 0xB0 * (unsigned long)i, seat) || seat->camera_point_count <= 0)
					continue;
				for (q = 0; q < v->seat_count && strcmp(v->seats[q].label, seat->label); q++)
					;
				if (q == v->seat_count)
				{
					seat->track_slot = v->seat_track_count++;
					v->child_seat_count++;
					if (getenv("HALO_H2_DEBUG"))
						platform_log("h2 vehicles: %s child seat %s (%s): Halo 2's camera track, %ld points", v->name, seat->label,
							k->pieces[p].render_model, seat->camera_point_count);
				}
			}
		}
		for (i = 0; i < v->seat_count; i++)
		{
			if (v->seats[i].camera_point_count > 0 && !(k->own_collision && i == 0 && v->camera_point_count > 0))
				v->seats[i].track_slot = v->seat_track_count++;
			if (getenv("HALO_H2_DEBUG") && v->seats[i].camera_point_count > 0)
				platform_log("h2 vehicles: %s seat %s: Halo 2's camera track, %ld points, level %.2f back %.2f up (%s)", v->name,
					v->seats[i].label, v->seats[i].camera_point_count, v->seats[i].camera_points[v->seats[i].camera_point_count / 2][0],
					v->seats[i].camera_points[v->seats[i].camera_point_count / 2][2],
					v->seats[i].track_slot != NONE ? "a tag of its own" : "the own seat's");
		}
	}
	/* Halo 2's vehicle's vitality (its hlmt's damage info, 0x60: 0xF8 each,
	the maximum vitality at 0x28) */
	{
		long hlmt = h2v_tag_named(cache, 'hlmt', k->pieces[0].render_model), count;
		struct h2_cache_tag t;
		unsigned long info;

		if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &t) && h2_cache_block(cache, t.address + 0x60, 0xF8, &count, &info) && count > 0)
		{
			v->vitality = h2w_read_real(cache, info + 0x28);
			/* (its body's stun and recharge: 0x2C..0x38) */
			{
				int q;

				for (q = 0; q < 4; q++)
					v->body_regeneration[q] = h2w_read_real(cache, info + 0x2C + 4 * (unsigned long)q);
				/* (its shield: 0x8C, then (past its material) 0x94..0x9C) */
				v->shield[0] = h2w_read_real(cache, info + 0x8C);
				for (q = 1; q < 4; q++)
					v->shield[q] = h2w_read_real(cache, info + 0x94 + 4 * (unsigned long)(q - 1));
			}
			v->physics.damage_info_flags = h2_cache_read32(cache, info);
			/* (its damage seats at 0xD8, 0x14 each) */
			h2v_damage_seats_read(cache, v, info, FALSE);
		}
		/* (its child objects' - its turret's: the Warthog's gunner's seat is
		the chaingun's in Halo 2, damage.cpp function_db210 passes the
		vehicle's damage on to them) damage seats, and the nodes that are
		theirs alone */
		h2v_child_parts_read(cache, v);
		/* its damage sections, their responses and its regions' damage
		states (h2_vehicle_damage.c) */
		if (hlmt != NONE && !getenv("HALO_H2_NO_VEHICLE_DAMAGE"))
			h2v_damage_read(build, v, hlmt);
		/* its targets (0x68, 0x1C each: marker, size 4, cone 8, flags 0x14 -
		locked by human tracking 1, by plasma tracking 2 -, lock-on distance
		0x18): what the rocket launcher locks on (h2_projectiles.c) */
		if (hlmt != NONE && h2_cache_tag_get(cache, hlmt, &t) && h2_cache_block(cache, t.address + 0x68, 0x1C, &count, &info))
		{
			long q;

			for (q = 0; q < count; q++)
			{
				unsigned long target = info + 0x1C * q;
				unsigned long flags = h2_cache_read32(cache, target + 0x14) & 3;
				float distance = h2w_read_real(cache, target + 0x18);

				if (!flags)
					continue;
				v->lock.tag = H2_LOCK_PORT_TAG;
				v->lock.flags |= (flags & 1 ? FLAG(_h2_lock_human_bit) : 0) | (flags & 2 ? FLAG(_h2_lock_plasma_bit) : 0);
				if (distance > v->lock.distance)
				{
					v->lock.distance = distance;
					v->lock.size = h2w_read_real(cache, target + 4);
				}
			}
			platform_log("h2 vehicles: %s: %ld targets, lockable %lx within %.0f (size %.2f)", v->name, count, v->lock.flags, v->lock.distance, v->lock.size);
		}
	}
	/* (a Gauss Warthog's gun: Halo 2's barrel - rounds a second, its
	projectile's speed and damage) */
	if (k->gun)
	{
		long gun = h2v_tag_named(cache, 'weap', k->gun);
		struct h2_cache_tag t, projectile_tag, damage_tag;
		long count;
		unsigned long block;

		if (gun != NONE && h2_cache_tag_get(cache, gun, &t) &&
			h2_cache_block(cache, t.address + 0x2D0, 0xEC, &count, &block) && count > 0)
		{
			long projectile = h2_cache_reference(cache, block + 0x8C);
			long effect_count;
			unsigned long effects;

			/* (its firing effect's sound - the barrel's firing effects at
			0xE4, 0x34 each, the effect a reference at 4 -, else the gauss
			rifle's own: the stand-in gun's is the chaingun's) */
			if (h2_cache_block(cache, block + 0xE4, 0x34, &effect_count, &effects) && effect_count > 0)
			{
				unsigned char const *q = h2_cache_at(cache, effects + 4, 8);

				v->gun_sound = q ? h2w_effect_sound(cache, h2_cache_tag_from_handle(cache, read32(q + 4))) : NONE;
			}
			if (v->gun_sound == NONE && k->gun_bolt)
				v->gun_sound = h2w_sound(cache, h2v_tag_named(cache, 'snd!', "sound\\weapons\\gauss_rifle\\fire"));
			v->gun_rounds_per_second = h2w_read_real(cache, block + 4);
			v->gun_illumination_recovery = h2w_read_real(cache, block + 0xA0);
			/* (a chaingun's spin: its rate from the minimum to the
			maximum over its acceleration time, back over its deceleration
			time; its error angles) */
			v->gun_rounds_per_second_maximum = h2w_read_real(cache, block + 8);
			/* (a second barrel with a projectile of its own: a second
			trigger - its bolt the kit rocket's, h2v_gun2_write) */
			/* (not the Scorpion's: its stand-in's own second trigger, its
			machine gun, is the cannon's second barrel already) */
			if (count > 1 && !getenv("HALO_H2_NO_SECOND_TRIGGER") && strcmp(k->base, "vehicles\\scorpion\\scorpion_mp"))
			{
				unsigned long barrel = block + 0xEC;
				long projectile2 = h2_cache_reference(cache, barrel + 0x8C);
				struct h2_cache_tag p2;

				if (projectile2 != NONE && h2_cache_tag_get(cache, projectile2, &p2))
				{
					float recovery = h2w_read_real(cache, barrel + 0x20);

					v->gun2_rate = h2w_read_real(cache, barrel + 4);
					if (v->gun2_rate <= 0.0f && recovery > 0.0f)
						v->gun2_rate = 1.0f / recovery;
					v->gun2_velocity = h2w_read_real(cache, p2.address + 0x17C);
					v->gun2_gravity = h2w_read_real(cache, p2.address + 0x164);
					v->gun2_guided = h2w_read_real(cache, p2.address + 0x184);
					v->gun2_projectile = getenv("HALO_H2_BOMB_KIT_DAMAGE") ? NONE : projectile2;
					platform_log("h2 vehicles: %s: its second barrel %.2f a second, %s at %.1f wu/s, gravity %.2f, guided %.1f deg/s", v->name,
						v->gun2_rate, h2_cache_tag_name(cache, projectile2), v->gun2_velocity, v->gun2_gravity, v->gun2_guided * 57.29578f);
				}
			}
			/* (the Scorpion's machine gun, its stand-in's second trigger:
			Halo 2's second barrel's bullet's damage on it) */
			if (count > 1 && !strcmp(k->base, "vehicles\\scorpion\\scorpion_mp"))
			{
				long projectile2 = h2_cache_reference(cache, block + 0xEC + 0x8C);
				struct h2_cache_tag p2;

				if (projectile2 != NONE && h2_cache_tag_get(cache, projectile2, &p2))
					v->gun2_impact_tag = h2_cache_reference(cache, p2.address + 0x148);
			}
			v->gun_rate_times[0] = h2w_read_real(cache, block + 0xC);
			v->gun_rate_times[1] = h2w_read_real(cache, block + 0x10);
			v->gun_error[0] = h2w_read_real(cache, block + 0x74);
			v->gun_error[1] = h2w_read_real(cache, block + 0x78);
			/* (a single shot's: its recovery its time) */
			if (v->gun_rounds_per_second <= 0.0f && h2w_read_real(cache, block + 0x20) > 0.0f)
				v->gun_rounds_per_second = 1.0f / h2w_read_real(cache, block + 0x20);
			if (projectile != NONE && h2_cache_tag_get(cache, projectile, &projectile_tag))
			{
				long damage = h2_cache_reference(cache, projectile_tag.address + 0x148);

				/* (its impact's, and its detonation's: Halo 2's shell and
				mortar explode, the stand-in's detonation carrying Halo 2's
				damage - not the kit's shell's or rocket's explosion on top) */
				v->gun_impact = damage != NONE;
				v->gun_detonation_tag = h2_cache_reference(cache, projectile_tag.address + 0x100);
				if (damage == NONE)
					damage = v->gun_detonation_tag;
				v->gun_velocity = h2w_read_real(cache, projectile_tag.address + 0x17C);
				v->gun_gravity = h2w_read_real(cache, projectile_tag.address + 0x164);
				if (damage != NONE && h2_cache_tag_get(cache, damage, &damage_tag))
				{
					v->gun_damage_tag = damage;
					v->gun_radius[0] = h2w_read_real(cache, damage_tag.address);
					v->gun_radius[1] = h2w_read_real(cache, damage_tag.address + 4);
					v->gun_damage[0] = h2w_read_real(cache, damage_tag.address + 0x1C);
					v->gun_damage[1] = h2w_read_real(cache, damage_tag.address + 0x20);
					v->gun_damage[2] = h2w_read_real(cache, damage_tag.address + 0x24);
				}
			}
		}
	}
	v->physics.secondary_on_melee = v->gun2_rate > 0.0f;
	if (getenv("HALO_H2_DEBUG"))
		fprintf(stderr, "h2 vehicles: %s: model %ld (%ld nodes, %ld markers, %ld parts)%s gun %.1f..%.1f/s %.0f wu/s damage %.0f..%.0f radius %.1f\n",
			v->name, v->model.model, v->model.node_count, v->model.marker_count, build->models[v->model.model].part_count,
			k->gun ? "," : "", v->gun_rounds_per_second, v->gun_rounds_per_second_maximum, v->gun_velocity, v->gun_damage[0], v->gun_damage[2],
			v->gun_radius[1]);
	if (getenv("HALO_H2_DEBUG_MARKERS"))
	{
		long i;

		for (i = 0; i < v->model.marker_count; i++)
		{
			struct h2w_marker const *mk = &v->model.markers[i];

			fprintf(stderr, "h2 vehicles: %s marker '%s' node %d (%s) at %.3f %.3f %.3f rot %.3f %.3f %.3f %.3f\n", v->name, mk->name, mk->node,
				mk->node >= 0 && mk->node < v->model.node_count ? v->model.nodes[mk->node].name : "?",
				mk->translation[0], mk->translation[1], mk->translation[2], mk->rotation[0], mk->rotation[1], mk->rotation[2], mk->rotation[3]);
		}
	}
	return v;
}

/* (a campaign level's) whether its scenario's vehicle palette (0x78, 0x28
each) has one of the name (its last part: "pelican"; a gravity throne's
"gravity_throne_no_flare" too) */
H2_ALIGN static boolean h2v_placed_campaign(struct h2_cache *cache, char const *collection)
{
	struct h2_cache_tag scenario;
	long count, i;
	unsigned long palette;
	size_t length = strlen(collection);

	if (!strstr(h2_cache_scenario_path(cache), "\\solo\\") || !h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x78, 0x28, &count, &palette))
	{
		return FALSE;
	}
	for (i = 0; i < count; i++)
	{
		long tag = h2_cache_reference(cache, palette + 0x28 * (unsigned long)i);
		char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;
		char const *last = name ? strrchr(name, '\\') : NULL;

		if (last && (!strcmp(h2_level_vehicle_alias(last + 1), collection) ||
			(!strncmp(last + 1, collection, length) && !strcmp(last + 1 + length, "_no_flare"))))
			return TRUE;
	}
	/* (the plasma cannon a heavy Grunt carries: c_turret_mp_item in the weapon
	palette, 0x98, its tag's deployed vehicle: on 04a, 05a and 05b it is in no
	vehicle palette, and cs_deploy_turret - h2_ai.c - unpacks it) */
	if (!strcmp(collection, "c_turret_mp") && h2_cache_block(cache, scenario.address + 0x98, 0x28, &count, &palette))
	{
		for (i = 0; i < count; i++)
		{
			long tag = h2_cache_reference(cache, palette + 0x28 * (unsigned long)i);
			char const *name = tag != NONE ? h2_cache_tag_name(cache, tag) : NULL;

			if (name && strstr(name, "c_turret_mp_item"))
				return TRUE;
		}
	}
	return FALSE;
}

/* whether the level's scenario places a vehicle of a collection (its
netgame items, 0x120, 0x90 each: the item or vehicle collection at 0x58,
by its name's last part: "multiplayer\\single_vehicles\\banshee") */
H2_ALIGN static boolean h2v_placed(struct h2_cache *cache, char const *collection)
{
	struct h2_cache_tag scenario;
	long count, i;
	unsigned long items;

	char swapped[32];

	if (!h2_cache_tag_get(cache, h2_cache_scenario_tag(cache), &scenario) ||
		!h2_cache_block(cache, scenario.address + 0x120, 0x90, &count, &items))
	{
		return FALSE;
	}
	/* (a test's swap, h2_level.c's netgame_stand_in: "from:to" places "to"
	where "from" is) */
	if (getenv("HALO_H2_VEHICLE_SWAP") && strchr(getenv("HALO_H2_VEHICLE_SWAP"), ':') &&
		!strcmp(strchr(getenv("HALO_H2_VEHICLE_SWAP"), ':') + 1, collection) &&
		(size_t)(strchr(getenv("HALO_H2_VEHICLE_SWAP"), ':') - getenv("HALO_H2_VEHICLE_SWAP")) < sizeof(swapped))
	{
		snprintf(swapped, sizeof(swapped), "%.*s", (int)(strchr(getenv("HALO_H2_VEHICLE_SWAP"), ':') - getenv("HALO_H2_VEHICLE_SWAP")),
			getenv("HALO_H2_VEHICLE_SWAP"));
		collection = swapped;
	}
	for (i = 0; i < count; i++)
	{
		long tag = h2_cache_reference(cache, items + 0x90 * (unsigned long)i + 0x58);
		/* (a placement by classification alone: its vehicle's,
		h2_level.c's h2_netgame_collection_name) */
		char const *name = h2_netgame_collection_name(cache, items + 0x90 * (unsigned long)i, tag);
		char const *last = name ? strrchr(name, '\\') : NULL;

		if (last && !strcmp(last + 1, collection))
			return TRUE;
	}
	return h2v_placed_campaign(cache, collection);
}

/* ---------- private code: h2_weapons.c's hooks' */

H2_ALIGN static void h2_vehicles_gather(struct build *build)
{
	long k;

	h2v.count = 0;
	h2v.script_damage_count = 0;
	h2v.cache_for_names = build->cache;
	h2v.kit_rocket = h2v.kit_bolt = h2v.kit_smoke = h2v.kit_fire_explosion = NONE;
	/* (the players' collision damage limits: a player is what a vehicle
	splatters, the Spartan's and the Elite's the same) */
	{
		long biped = h2v_tag_named(build->cache, 'bipd', "objects\\characters\\masterchief\\masterchief_mp");
		struct h2_cache_tag t;
		int q;

		memset(h2v.splatter, 0, sizeof(h2v.splatter));
		if (biped != NONE && h2_cache_tag_get(build->cache, biped, &t))
		{
			for (q = 0; q < 4; q++)
				h2v.splatter[q] = h2w_read_real(build->cache, t.address + 0x70 + 4 * q);
		}
	}
	if (getenv("HALO_H2_NO_VEHICLES"))
		return;
	h2v.board_melee = NONE;     /* (found after the vehicles: their tags first) */
	h2v.debris_count = 0;
	h2v.extra_debris_count = 0;
	h2v.blast_count = 0;
	h2v.cache_for_names = build->cache;
	h2_vehicle_damage_definitions_clear();
	for (k = 0; k < (long)(sizeof(h2v_kinds) / sizeof(h2v_kinds[0])) && h2v.count < H2V_MAXIMUM_VEHICLES; k++)
	{
		struct h2v_vehicle *v;

		/* (a vehicle only some levels have: made where the level places
		one - HALO_H2_ALL_VEHICLES every one) */
		if (h2v_kinds[k].placed_only && !getenv("HALO_H2_ALL_VEHICLES") && !h2v_placed(build->cache, h2v_kinds[k].collection))
			continue;
		/* (Halo 3's rows are h3_vehicles_gather's; a Halo 2 kind Halo 3's
		vehicle of the collection replaces is not made) */
		if (h2v_kinds[k].h3 || h3_vehicles_replaces(k))
			continue;
		/* (Halo 2's main menu: no vehicle placed, none's models in its map -
		each tried was "not made": not tried, piece H) */
		if (h2_level_is_main_menu(build->cache))
			continue;
		{
			char unit[96];

			snprintf(unit, sizeof(unit), "the vehicle %s", h2v_kinds[k].collection);
			h2_build_heap_unit(unit);
		}
		v = h2v_vehicle_make(build, k);
		/* (its own graph, Halo 2's animations: HALO_H2_STANDIN_VEHICLE_GRAPH the stand-in's) */
		if (v && h2v_kinds[k].own_graph && !getenv("HALO_H2_STANDIN_VEHICLE_GRAPH"))
			v->graph = h2v_graph_make(build, v);

		if (v)
		{
			/* (after the vehicles before it: the blasts, debris and
			boarding melee are counted after all of them) */
			v->tag = h2_vehicles_tag_count() - h2v.blast_count;
			h2v.vehicles[h2v.count++] = v;
		}
	}
	h3_vehicles_gather(build);
	/* (each vehicle's name in Halo 2's words: its HUD strings'
	"<name>_flip", "to flip Shadow" - the prompts' names, hud.c) */
	h2_vehicle_prompt_names_clear();
	{
		static char const *const flips[][2] =
		{
			{ "warthog", "warthog_flip" }, { "ghost", "ghost_flip" }, { "banshee", "banshee_flip" }, { "scorpion", "scorpion_flip" },
			{ "spectre", "spectre_flip" }, { "wraith", "wraith_flip" }, { "creep", "shadow_flip" },
		};
		long unic = h2_cache_tag_find(build->cache, 'unic', "ui\\hud\\hud_messages");
		struct h2_cache_tag list;
		unsigned char const *p;
		long first = 0, count = 0, q, i, f;

		if (unic != NONE && h2_cache_tag_get(build->cache, unic, &list) && (p = h2_cache_at(build->cache, list.address + 0x10, 4)) != NULL)
		{
			first = (unsigned short)read16(p);
			count = (unsigned short)read16(p + 2);
		}
		for (q = 0; q < count; q++)
		{
			unsigned long id = 0;
			char words[128], name[64];
			char const *at;

			if (!h2_cache_localized_string(build->cache, 0, first + q, &id, words, sizeof(words)) ||
				!h2_cache_string(build->cache, id, name, sizeof(name)) || !(at = strstr(words, "to flip ")))
			{
				continue;
			}
			for (f = 0; f < (long)(sizeof(flips) / sizeof(flips[0])); f++)
			{
				if (strcmp(name, flips[f][1]))
					continue;
				for (i = 0; i < h2v.count; i++)
				{
					char tag[64];

					if (strncmp(h2v.vehicles[i]->name, flips[f][0], strlen(flips[f][0])))
						continue;
					snprintf(tag, sizeof(tag), "h2\\vehicles\\%s", h2v.vehicles[i]->name);
					h2_vehicle_prompt_name_define(tag, at + 8);
				}
			}
		}
	}
	h2v.board_melee = h2v.count && h2_caps_boarding() ?
		h2_cache_tag_find(build->cache, 'jpt!', "objects\\characters\\masterchief\\damage_effects\\masterchief_boarding_melee") : NONE;
	h2v.debris_count = 0;
	for (k = 0; k < (long)(sizeof(h2v_debris_names) / sizeof(h2v_debris_names[0])) + h2v.extra_debris_count && h2v.count &&
		h2v.debris_count < H2V_MAXIMUM_DEBRIS && !getenv("HALO_H2_NO_DEBRIS"); k++)
	{
		long fixed = (long)(sizeof(h2v_debris_names) / sizeof(h2v_debris_names[0]));
		char const *debris_name = k < fixed ? h2v_debris_names[k][0] : h2v.extra_debris[k - fixed][0];
		long tag = h2v_tag_named(build->cache, 'mode', k < fixed ? h2v_debris_names[k][1] : h2v.extra_debris[k - fixed][1]);
		struct h2v_debris *d = tag != NONE ? calloc(1, sizeof(*d)) : NULL;

		if (!d)
			continue;
		snprintf(d->name, sizeof(d->name), "%s", debris_name);
		h2w_model_begin(&d->model);
		d->model.levels = TRUE;
		if (h2w_model_add(build, &d->model, tag, NULL, h2w_detail("HALO_H2_VEHICLE_DETAIL", H2W_VEHICLE_DETAIL_DEFAULT), FALSE) && h2w_model_finish(build, &d->model))
		{
			struct model_made const *made = &build->models[d->model.model];

			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 vehicles: debris %s: %ld nodes, %ld parts, radius %.3f, extents %.3f..%.3f %.3f..%.3f %.3f..%.3f, node 0 scale %.3f",
					d->name, d->model.node_count, made->part_count, made->radius, made->extent[0], made->extent[1], made->extent[2],
					made->extent[3], made->extent[4], made->extent[5], d->model.node_count ? d->model.nodes[0].scale : 0.0f);
			h2v.debris[h2v.debris_count++] = d;
		}
		else
			free(d);
	}
	/* (the scripts' damage effects - Halo 2's value type damage, 34 -: a
	campaign level's, its jpt!s alone; HALO_H2_NO_SCRIPT_DAMAGE=1: none, as
	before) */
	if (!build->multiplayer && !getenv("HALO_H2_NO_SCRIPT_DAMAGE"))
	{
		long tags[32], count = h2_scripts_constant_tags(build->cache, 34, tags, 32), t;
		struct h2_cache_tag d;

		for (t = 0; t < count; t++)
		{
			if (h2_cache_tag_get(build->cache, tags[t], &d) && d.group_tag == 'jpt!')
				h2v.script_damages[h2v.script_damage_count++] = tags[t];
		}
		if (h2v.script_damage_count)
			platform_log("h2 vehicles: %ld of the scripts' damage effects made", h2v.script_damage_count);
	}
}

/* a tag's id as its instance has it (a kit tag's is the kit's own, not
tag_id's: the tag check knows a reference by it), tag_id's if none */
H2_ALIGN static unsigned long h2v_instance_id(struct region *r, unsigned long instances, long index)
{
	unsigned long id = read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 12));

	return id && (long)(id & 0xFFFF) == index ? id : tag_id(index);
}

/* whether the vehicle is a turret (Halo 2's anti-personnel turret, its
plasma cannon): type 6, fixed, its gunner's seat alone */
H2_ALIGN static boolean h2v_turret(struct h2v_vehicle const *v)
{
	return !strcmp(v->name, "h_turret_ap") || !strcmp(v->name, "c_turret_mp") || !strcmp(v->name, "h_turret_mp") ||
		!strcmp(v->name, "c_turret_ap") || !strcmp(v->name, "cov_guntower_turret") ||
		/* (Halo 3's mounted turrets, h3_vehicles.c) */
		(h2v_kinds[v->kind].h3 && (!strcmp(v->name, "machinegun_turret") || !strcmp(v->name, "plasma_cannon") || !strcmp(v->name, "shade")));
}

/* whether the vehicle has its own collision model and seat camera (its
kind's, and Halo 2's both read) */
H2_ALIGN static boolean h2v_own_collision(struct h2v_vehicle const *v)
{
	return h2v_kinds[v->kind].own_collision && v->h2_collision != NONE && v->h2_render_model != NONE && v->camera_point_count > 0;
}

H2_ALIGN static long h2v_tags(struct h2v_vehicle const *v)
{
	/* (its vehicle, its gun's, its force model's values, and (own
	collision) its collision model and its seat's camera track) */
	return (h2v_kinds[v->kind].gun ? 1 + H2V_GUN_TAGS : 1) + 1 + (h2v_own_collision(v) ? 2 : 0) + (h2v_kinds[v->kind].own_body ? 1 : 0) +
		(v->graph ? 1 : 0) + (v->gun2_rate > 0.0f ? H2V_BOMB_TAGS : 0) + v->seat_track_count;
}

/* (own graph) its graph's tag: after its body's */
H2_ALIGN static long h2v_body_tag(struct h2v_vehicle const *v);
H2_ALIGN static long h2v_graph_tag(struct h2v_vehicle const *v)
{
	return h2v_body_tag(v) + (h2v_kinds[v->kind].own_body ? 1 : 0);
}

/* (a second barrel) its projectile's tag: after its graph's */
H2_ALIGN static long h2v_gun2_tag(struct h2v_vehicle const *v)
{
	return h2v_graph_tag(v) + (v->graph ? 1 : 0);
}

/* its seats' camera tracks' tags (Halo 2's, seat_track_count of them, by
each seat's track_slot): its last, after its bomb's */
H2_ALIGN static long h2v_seat_track_tag(struct h2v_vehicle const *v, long slot)
{
	return h2v_gun2_tag(v) + (v->gun2_rate > 0.0f ? H2V_BOMB_TAGS : 0) + slot;
}

/* the seat (its own, or its child objects') whose camera track is slot n,
else of the label (label non-NULL); NULL none */
H2_ALIGN static struct h2v_seat const *h2v_camera_seat(struct h2v_vehicle const *v, long slot, char const *label)
{
	long q;

	for (q = 0; q < v->seat_count + v->child_seat_count; q++)
	{
		struct h2v_seat const *seat = q < v->seat_count ? &v->seats[q] : &v->child_seats[q - v->seat_count];

		if (label ? !strcmp(seat->label, label) : seat->track_slot == slot && slot != NONE)
			return seat;
	}
	return NULL;
}

/* (own body) its physics' tag: after its force model's values and its
collision model and camera track */
H2_ALIGN static long h2v_body_tag(struct h2v_vehicle const *v)
{
	return v->tag + (h2v_kinds[v->kind].gun ? 1 + H2V_GUN_TAGS : 1) + 1 + (h2v_own_collision(v) ? 2 : 0);
}

/* the force model's values' tag (from the vehicles' first; its collision
model's and camera track's after it) */
H2_ALIGN static long h2v_physics_tag(struct h2v_vehicle const *v)
{
	return v->tag + (h2v_kinds[v->kind].gun ? 1 + H2V_GUN_TAGS : 1);
}

/* (h2_models_load.inc) the vehicles' own collision models' tags (from the
vehicles' first), out: their count */
H2_ALIGN static long h2_vehicles_collision_tags(long *out, long maximum)
{
	long i, count = 0;

	for (i = 0; i < h2v.count && count < maximum; i++)
	{
		if (h2v_own_collision(h2v.vehicles[i]))
			out[count++] = h2v_physics_tag(h2v.vehicles[i]) + 1;
	}
	return count;
}

/* (h2_models_load.inc, at load) vehicle i's own collision model written at
root (664 bytes allocated) as h2_vehicles_write writes it: FALSE if it
could not be (the level's builder leaves it a root with no BSPs then, as
this does) */
H2_ALIGN static boolean h2v_collision_nodes_write(struct build *build, struct region *r, unsigned long root, struct h2v_vehicle const *v);
H2_ALIGN static boolean h2_vehicles_collision_write(struct build *build, struct region *r, unsigned long root, long i)
{
	struct h2v_vehicle const *v = h2v.vehicles[i];
	struct object_made made;

	memset(&made, 0, sizeof(made));
	made.h2_tag = NONE;
	made.model = NONE;
	made.collision = v->h2_collision;
	made.render_model = v->h2_render_model;
	made.vitality = 1.0f;
	snprintf(made.name, sizeof(made.name), "%s", v->name);
	if (!object_collision_write(build, r, root, &made) || !h2v_collision_nodes_write(build, r, root, v))
	{
		platform_log("h2 vehicles: %s: its collision model could not be made", v->name);
		return FALSE;
	}
	return TRUE;
}

H2_ALIGN static long h2_vehicles_tag_count(void)
{
	long i, count = 0;

	for (i = 0; i < h2v.count; i++)
		count += h2v_tags(h2v.vehicles[i]);
	return count + h2v.blast_count + h2v.debris_count + h2v.script_damage_count + (h2v.board_melee != NONE ? 1 : 0);
}

/* the scripts' damage effects' first tag (from the vehicles' first): after
the debris, before the boarding melee */
H2_ALIGN static long h2v_script_damage_first(void)
{
	return h2_vehicles_tag_count() - (h2v.board_melee != NONE ? 1 : 0) - h2v.script_damage_count;
}

/* the blasts' first tag (from the vehicles' first): before the debris */
H2_ALIGN static long h2v_blast_first(void)
{
	return h2v_script_damage_first() - h2v.debris_count - h2v.blast_count;
}

/* the debris' first tag (from the vehicles' first) */
H2_ALIGN static long h2v_debris_first(void)
{
	return h2v_script_damage_first() - h2v.debris_count;
}

H2_ALIGN static void h2_vehicles_tag_name(long index, unsigned long *group, char *text, long size)
{
	static unsigned long const groups[1 + H2V_GUN_TAGS] = { 'vehi', 'weap', 'proj', 'jpt!', 'effe', 'jpt!', 'effe' };
	static char const *const kinds[1 + H2V_GUN_TAGS] = { "", " gun", " gun bolt", " gun damage", " gun fire", " gun detonation damage",
		" gun detonation" };
	long i;

	if (h2v.board_melee != NONE && index == h2_vehicles_tag_count() - 1)
	{
		*group = 'jpt!';
		snprintf(text, size, "h2\\vehicles\\boarding melee");
		return;
	}
	if (index >= h2v_blast_first() && index < h2v_debris_first())
	{
		char const *name = h2_cache_tag_name(h2v.cache_for_names, h2v.blasts[index - h2v_blast_first()]);
		char const *last = strrchr(name, '\\');

		*group = 'jpt!';
		snprintf(text, size, "h2\\vehicles\\damage\\%s", last ? last + 1 : name);
		return;
	}
	if (index >= h2v_script_damage_first())
	{
		*group = 'jpt!';
		snprintf(text, size, "h2\\%s", h2_cache_tag_name(h2v.cache_for_names, h2v.script_damages[index - h2v_script_damage_first()]));
		return;
	}
	if (index >= h2v_debris_first())
	{
		*group = 'garb';
		snprintf(text, size, "h2\\vehicles\\debris\\%s", h2v.debris[index - h2v_debris_first()]->name);
		return;
	}
	for (i = 0; i + 1 < h2v.count && index >= h2v.vehicles[i + 1]->tag; i++)
		;
	if (index == h2v_physics_tag(h2v.vehicles[i]))
	{
		*group = 'str#';
		snprintf(text, size, "h2\\vehicles\\%s physics", h2v.vehicles[i]->name);
		return;
	}
	if (h2v.vehicles[i]->gun2_rate > 0.0f && index >= h2v_gun2_tag(h2v.vehicles[i]) && index < h2v_gun2_tag(h2v.vehicles[i]) + H2V_BOMB_TAGS)
	{
		static char const *const bomb_kinds[H2V_BOMB_TAGS] = { "", " impact", " detonation", " attached", " super", " attached super",
			" detonation effect", " super effect" };
		long k = index - h2v_gun2_tag(h2v.vehicles[i]);

		*group = k == 0 ? 'proj' : k >= 2 + H2W_SLOT_DETONATION_EFFECT - H2W_SLOT_DETONATION_DAMAGE ? 'effe' : 'jpt!';
		snprintf(text, size, "h2\\vehicles\\%s gun bomb%s", h2v.vehicles[i]->name, bomb_kinds[k]);
		return;
	}
	if (h2v.vehicles[i]->seat_track_count > 0 && index >= h2v_seat_track_tag(h2v.vehicles[i], 0) &&
		index < h2v_seat_track_tag(h2v.vehicles[i], h2v.vehicles[i]->seat_track_count))
	{
		struct h2v_seat const *seat = h2v_camera_seat(h2v.vehicles[i], index - h2v_seat_track_tag(h2v.vehicles[i], 0), NULL);

		*group = 'trak';
		snprintf(text, size, "h2\\vehicles\\%s %s camera", h2v.vehicles[i]->name, seat ? seat->label : "seat");
		return;
	}
	if (h2v.vehicles[i]->graph && index == h2v_graph_tag(h2v.vehicles[i]))
	{
		*group = 'antr';
		snprintf(text, size, "h2\\vehicles\\%s graph", h2v.vehicles[i]->name);
		return;
	}
	if (h2v_kinds[h2v.vehicles[i]->kind].own_body && index == h2v_body_tag(h2v.vehicles[i]))
	{
		*group = 'phys';
		snprintf(text, size, "h2\\vehicles\\%s body", h2v.vehicles[i]->name);
		return;
	}
	if (index == h2v_physics_tag(h2v.vehicles[i]) + 1)
	{
		*group = 'coll';
		snprintf(text, size, "h2\\vehicles\\%s collision", h2v.vehicles[i]->name);
		return;
	}
	if (index == h2v_physics_tag(h2v.vehicles[i]) + 2)
	{
		*group = 'trak';
		snprintf(text, size, "h2\\vehicles\\%s camera", h2v.vehicles[i]->name);
		return;
	}
	*group = groups[index - h2v.vehicles[i]->tag];
	snprintf(text, size, "h2\\vehicles\\%s%s", h2v.vehicles[i]->name, kinds[index - h2v.vehicles[i]->tag]);
}

/* the kit's vehicles the made ones come from, and the recipe's strips of
their models (none: a vehicle's stand-in's model is still the kit's - its
pixels are, until every one is made) */
H2_ALIGN static void h2_vehicles_kit_find(unsigned char *kit)
{
	long i;

	for (i = 0; i < h2v.count; i++)
		h2v.vehicles[i]->base_vehicle = scene_pack_find(kit, 'vehi', h2v_kinds[h2v.vehicles[i]->kind].base);
	h2v.kit_rocket = scene_pack_find(kit, 'proj', "weapons\\rocket launcher\\rocket");
	h2v.kit_bolt = scene_pack_find(kit, 'proj', "weapons\\plasma rifle\\charged bolt");
	h2v.kit_item = scene_pack_find(kit, 'weap', "weapons\\assault rifle\\assault rifle");
	h2v.kit_damage = scene_pack_find(kit, 'jpt!', "weapons\\assault rifle\\bullet");
	h2v.kit_blast = scene_pack_find(kit, 'jpt!', "weapons\\frag grenade\\explosion");
	h2v.kit_smoke = scene_pack_find(kit, 'effe', "weapons\\frag grenade\\effects\\smoke");
	h2v.kit_fire_explosion = scene_pack_find(kit, 'effe', "weapons\\frag grenade\\effects\\fire explosion");
	if (h2v.kit_fire_explosion == NONE)
		h2v.kit_fire_explosion = scene_pack_find(kit, 'effe', "weapons\\frag grenade\\effects\\explosion");
}

/* the kit's recipe's strips for the made vehicles (text, its length):
their stand-ins' models (Halo 2's are drawn), and the globals' Scorpion (no
Halo 2 multiplayer map here has one); its new length */
H2_ALIGN static long h2_vehicles_kit_strips(char *text, long size, long length)
{
	long k;
	boolean scorpion = FALSE;

	if (!h2v.count)
		return length;
	for (k = 0; k < h2v.count && length < size; k++)
	{
		char const *base = h2v_kinds[h2v.vehicles[k]->kind].base;
		char const *model = !strcmp(base, "vehicles\\ghost\\ghost_mp") ? "vehicles\\ghost\\ghost" :
			!strcmp(base, "vehicles\\scorpion\\scorpion_mp") ? "vehicles\\scorpion\\scorpion_mp\\scorpion_mp" : base;
		char line[300];

		/* (the Scorpion: the stand-in's own tags, not the kit recipe's) */
		if (!strcmp(base, "vehicles\\scorpion\\scorpion_mp"))
		{
			scorpion = TRUE;
			snprintf(line, sizeof(line), "tag vehi %s\n", base);
			if (!strstr(text, line))
				length += snprintf(text + length, size - length, "%s", line);
		}

		snprintf(line, sizeof(line), "strip mode %s\n", model);
		if (!strstr(text, line))
			length += snprintf(text + length, size - length, "%s", line);
	}
	if (length < size && !scorpion)
		length += snprintf(text + length, size - length, "strip vehi vehicles\\scorpion\\scorpion_mp\n");
	return length;
}

/* the made vehicle a netgame collection's name stands for (its tag index
from the vehicles' first), NONE if none */
H2_ALIGN static long h2_vehicles_for(char const *collection)
{
	long i;

	for (i = 0; i < h2v.count; i++)
	{
		if (!strcmp(collection, h2v_kinds[h2v.vehicles[i]->kind].collection))
			return h2v.vehicles[i]->tag;
	}
	return NONE;
}

/* the debris' tags: garbage, the kit's assault rifle's item fields (its
bounce sound and material effects), Halo 2's piece's model, no collision
model (an item rolls on its bounding sphere) */
H2_ALIGN static void h2v_debris_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first)
{
	unsigned long base_root = h2v.kit_item != NONE ? h2w_root(r, instances, h2v.kit_item) : 0;
	long i;

	for (i = 0; i < h2v.debris_count && !r->b.failed; i++)
	{
		struct h2v_debris *d = h2v.debris[i];
		long index = first + h2v_debris_first() + i;
		unsigned long root;
		float radius = build->models[d->model.model].radius;
		static unsigned long const blocks[] = { offsetof(struct item_definition, object.attachments), offsetof(struct item_definition, object.widgets),
			offsetof(struct item_definition, object.functions), offsetof(struct item_definition, object.change_colors),
			offsetof(struct item_definition, object.predicted_resources) };
		int b;

		h2w_model_write(r, h2w_root(r, instances, model_first + d->model.model), &d->model);
		root = base_root ? h2w_tag_copy(r, base_root, sizeof(struct item_definition), 'garb') : region_alloc(r, sizeof(struct item_definition));
		if (r->b.failed)
			break;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 20), r->link + root);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 4), 'item');
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 8), 'obje');
		write16(region_at(r, root + offsetof(struct item_definition, object.type)), 4);   /* (garbage) */
		writef(region_at(r, root + offsetof(struct item_definition, object.bounding_radius)), radius > 0.05f ? radius : 0.05f);
		writef(region_at(r, root + offsetof(struct item_definition, object.render_bounding_radius)), radius > 0.05f ? radius : 0.05f);
		region_reference(r, root + offsetof(struct item_definition, object.model), 'mode', tag_id(model_first + d->model.model));
		region_reference(r, root + offsetof(struct item_definition, object.animation_graph), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, object.collision_model), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, object.physics), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, object.modifier_shader), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, object.creation_effect), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, item.detonating_effect), 0, 0xFFFFFFFFUL);
		region_reference(r, root + offsetof(struct item_definition, item.detonation_effect), 0, 0xFFFFFFFFUL);
		for (b = 0; b < (int)(sizeof(blocks) / sizeof(blocks[0])); b++)
			region_block(r, root + blocks[b], 0, 4);
		write16(region_at(r, root + offsetof(struct item_definition, item.hud_message_index)), -1);
		/* (its own size: the game engine draws an item lying in a multiplayer
		game at its item scale - the assault rifle's made the tires huge) */
		if (getenv("HALO_H2_DEBRIS_KIT_SCALE"))
			platform_log("h2 vehicles: debris %s keeps the kit's item scale %.2f (a test's)", d->name,
				readf(region_at(r, root + offsetof(struct item_definition, item.scale))));
		else
			writef(region_at(r, root + offsetof(struct item_definition, item.scale)), 1.0f);
	}
}

/* (h2_caps_boarding) a vehicle's seats made Halo 2's: the stand-in's, the
Warthog's passenger seen in third person (Halo 2 has no first person in a
vehicle), and a boarding seat for each seat Halo 2 boards - at its boarding
marker, labelled as the players' graphs' boarding mode (h2_animation.c:
its enter, then its board as its idle), no driver's or gunner's, unused[0]
the seat it boards (units.c's unit_seat_boarding_target). The seats are
this level's own block: the kit's stand-in keeps its */
static struct
{
	char const *collection_prefix;  /* (the made vehicle's netgame name begins so) */
	char const *seat;               /* the seat boarded (its label) */
	char const *label;              /* the boarding seat's */
	char const *marker;
	/* (Halo 2's seat on a tank's hull: its boarder beats on the hatch -
	the melee - or drops a grenade in - the grenade, at grenade_marker -
	in place of throwing the driver out: h2_vehicle_damage.c, which
	knows its grenade marker by its label) */
	boolean beats;
	char const *grenade_marker;     /* (Halo 2's; h2_vehicle_damage.c's table the same) */
} const h2v_boarding_seats[] =
{
	{ "warthog", "W-driver", "warthog_b_d", "warthog b d enter board" },
	{ "warthog", "W-passenger", "warthog_b_p", "warthog b p enter board" },
	{ "warthog", "W-gunner", "warthog_b_g", "warthog b g enter board" },
	{ "ghost", "G-driver", "ghost_b_b", "ghost b b enter board" },
	/* (from the side: Halo 2's ghost_b_l seat boards at the hood's
	marker, ghost_b_h) */
	{ "ghost", "G-driver", "ghost_b_l", "ghost b h enter board" },
	/* (the vehicles made since: Halo 2's boarding seats the players' graphs
	have a mode for - banshee_b_d, scorpion_b_d_l/_r, spectre_b_*; the
	Wraith's and the Scorpion's from above, beating on a hatch, are not
	jackings) */
	{ "banshee", "banshee_d", "banshee_b_d", "banshee b d enter board" },
	{ "scorpion", "scorpion_d", "scorpion_b_d_l", "scorpion b d l enter board", TRUE, "driver grenade" },
	{ "scorpion", "scorpion_d", "scorpion_b_d_r", "scorpion b d r enter board", TRUE, "driver grenade" },
	{ "scorpion", "scorpion_d", "scorpion_b_b", "scorpion b b enter board", TRUE, "grenade engine" },
	{ "wraith", "wraith_d", "wraith_b_l", "wraith b l enter board", TRUE, "grenade driver" },
	{ "wraith", "wraith_d", "wraith_b_r", "wraith b r enter board", TRUE, "grenade driver" },
	{ "spectre", "spectre_d", "spectre_b_d", "spectre b d enter board" },
	{ "spectre", "spectre_g", "spectre_b_g", "spectre b g enter board" },
	{ "spectre", "spectre_p_l", "spectre_b_p_l", "spectre b p l enter board" },
	{ "spectre", "spectre_p_r", "spectre_b_p_r", "spectre b p r enter board" },
	/* (a Prophet's gravity throne - 05b's Regret -: its boarder beats on
	the Prophet, Halo 2's throne_b_l/_r, their melee the seat's
	"training_melee") */
	{ "gravity_throne", "throne_d", "throne_b_l", "throne b l", TRUE, "driver grenade" },
	{ "gravity_throne", "throne_d", "throne_b_r", "throne b r", TRUE, "driver grenade" },
};

static short h2v_seat_text_gunner = NONE, h2v_seat_text_passenger = NONE;

H2_ALIGN static void h2v_seats_write(struct region *r, unsigned long root, struct h2v_vehicle const *v, long track_index, long first)
{
	enum { MAXIMUM_BOARDED_SEATS = 8 };
	long extra_first = NONE;
	unsigned long field = root + offsetof(struct unit_definition, unit.seats);
	long count = (long)read32(region_at(r, field)), boarded = 0, i, k;
	unsigned long from = read32(region_at(r, field + 4)), to;
	short targets[MAXIMUM_BOARDED_SEATS];
	short kinds[MAXIMUM_BOARDED_SEATS];
	struct unit_seat *seats;

	if (count <= 0 || count > 16 || !from)
		return;
	seats = malloc(sizeof(*seats) * (unsigned long)(count + MAXIMUM_BOARDED_SEATS + 16));
	if (!seats)
		return;
	memcpy(seats, region_at(r, from - r->link), sizeof(*seats) * (unsigned long)count);
	/* (its seats past the stand-in's - the Shadow's gunner and riders:
	copies of the stand-in seat named, a gunner's its weapon's, a rider's
	neither driver nor gunner, third person) */
	if (h2v_kinds[v->kind].extra_seats)
	{
		struct h2v_seat_name const *n;
		long base_count = count;

		extra_first = count;
		for (n = h2v_kinds[v->kind].extra_seats; n->ce && count < 16; n++)
		{
			for (i = 0; i < base_count && strcmp(seats[i].label, n->ce); i++)
				;
			if (i == base_count)
				continue;
			seats[count] = seats[i];
			snprintf(seats[count].label, sizeof(seats[count].label), "%s", n->h2);
			snprintf(seats[count].marker_name, sizeof(seats[count].marker_name), "%s", n->marker);
			seats[count].flags &= ~((1UL << _unit_seat_driver_bit) | (1UL << _unit_seat_gunner_bit));
			if (strstr(n->h2, "_g"))
			{
				seats[count].flags |= 1UL << _unit_seat_gunner_bit;
				if (h2v_seat_text_gunner != NONE)
					seats[count].icon_text_index = h2v_seat_text_gunner;
			}
			else
			{
				if (h2v_seat_text_passenger != NONE)
					seats[count].icon_text_index = h2v_seat_text_passenger;
				seats[count].flags |= 1UL << _unit_seat_third_person_camera_bit;
				seats[count].flags &= ~(1UL << _unit_seat_first_person_camera_bit);
			}
			seats[count].seat_huds.count = 0;
			seats[count].seat_huds.address = 0;
			seats[count].built_in_actor_reference.index = NONE;
			/* (its camera Halo 2's seat's marker and pitch range, where the
			model has the marker - the Shadow gunner's creep_g_camera, above
			the turret: the stand-in Ghost driver's camera put it inside the
			hull) */
			for (k = 0; k < v->seat_count && strcmp(v->seats[k].label, n->h2); k++)
				;
			if (k < v->seat_count && v->seats[k].camera_marker[0])
			{
				float place[3];

				if (h2v_marker_place(&v->model, v->seats[k].camera_marker, place))
				{
					snprintf(seats[count].camera.marker_name, sizeof(seats[count].camera.marker_name), "%s", v->seats[k].camera_marker);
					if (v->seats[k].camera_pitch[1] > v->seats[k].camera_pitch[0])
					{
						seats[count].camera.pitch_minimum = v->seats[k].camera_pitch[0];
						seats[count].camera.pitch_maximum = v->seats[k].camera_pitch[1];
					}
				}
				if (getenv("HALO_H2_DEBUG"))
					platform_log("h2 vehicles: %s seat %s: camera '%s' %s", v->name, n->h2, v->seats[k].camera_marker,
						seats[count].camera.marker_name[0] && !strcmp(seats[count].camera.marker_name, v->seats[k].camera_marker) ? "used" : "not on the model");
			}
			count++;
		}
	}
	/* (the anti-personnel turret: the stand-in Warthog's gunner's seat
	alone, seen in third person, Halo 2's turret's) */
	if (h2v_turret(v))
	{
		for (i = 0; i < count && strcmp(seats[i].label, "W-gunner"); i++)
			;
		if (i < count)
		{
			seats[0] = seats[i];
			count = 1;
			seats[0].flags |= 1UL << _unit_seat_third_person_camera_bit;
			seats[0].flags &= ~(1UL << _unit_seat_first_person_camera_bit);
		}
	}
	/* (a vehicle on another's rig: its seats at Halo 2's markers) */
	for (i = 0; i < count && h2v_kinds[v->kind].seat_names; i++)
	{
		struct h2v_seat_name const *n;

		for (n = h2v_kinds[v->kind].seat_names; n->ce && strcmp(n->ce, seats[i].label); n++)
			;
		if (!n->ce)
			continue;
		snprintf(seats[i].label, sizeof(seats[i].label), "%s", n->h2);
		if (n->marker)
			snprintf(seats[i].marker_name, sizeof(seats[i].marker_name), "%s", n->marker);
	}
	/* (a campaign vehicle on another's rig - the Phantom, the Pelican -: each Halo 2 seat
	its stand-in has none for its own, the stand-in's first seat's copy at
	Halo 2's marker - its troops' and cargo seats, no driver's or gunner's,
	seen in third person, no huds -, as many as this engine's 16 hold) */
	if (h2v_kinds[v->kind].seat_names && h2v_kinds[v->kind].own_graph &&
		(!strcmp(h2v_kinds[v->kind].collection, "phantom") || !strcmp(h2v_kinds[v->kind].collection, "pelican")))
	{
		long template = count;

		for (k = 0; k < v->seat_count && count < 16; k++)
		{
			struct unit_seat *b;

			for (i = 0; i < count && strcmp(seats[i].label, v->seats[k].label); i++)
				;
			if (i < count)
				continue;
			b = &seats[count++];
			*b = seats[0];
			snprintf(b->label, sizeof(b->label), "%s", v->seats[k].label);
			if (v->seats[k].marker[0])
			{
				char *c;

				/* (its markers named with spaces, h2_weapons.c's h2w_name) */
				snprintf(b->marker_name, sizeof(b->marker_name), "%s", v->seats[k].marker);
				for (c = b->marker_name; *c; c++)
				{
					if (*c == '_')
						*c = ' ';
				}
			}
			b->flags &= ~((1UL << _unit_seat_driver_bit) | (1UL << _unit_seat_gunner_bit) | (1UL << _unit_seat_first_person_camera_bit));
			b->flags |= 1UL << _unit_seat_third_person_camera_bit;
			b->seat_huds.count = 0;
			b->seat_huds.address = 0;
			/* (no camera tracks: a block is one seat's, the tag check's
			rule; no player rides these) */
			b->camera.unit_camera_tracks.count = 0;
			b->camera.unit_camera_tracks.address = 0;
			b->built_in_actor_reference.index = NONE;
		}
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 vehicles: %s: %ld seats of Halo 2's own added", v->name, count - template);
	}
	/* (each seat Halo 2's flags: who may sit there - units.c's
	unit_can_enter_seat: the Scorpion's riders are none of the players') */
	for (i = 0; i < count; i++)
	{
		for (k = 0; k < v->seat_count && strcmp(v->seats[k].label, seats[i].label); k++)
			;
		if (k < v->seat_count && !seats[i].unused[2])
		{
			seats[i].unused[1] = (long)v->seats[k].flags;
			seats[i].unused[2] = (long)UNIT_SEAT_H2_FLAGS_MAGIC;
			/* (and its entry radius: units.c's unit_find_nearby_seat) */
			memcpy(&seats[i].unused[3], &v->seats[k].entry_radius, sizeof(float));
		}
	}
	for (k = 0; k < (long)(sizeof(h2v_boarding_seats) / sizeof(h2v_boarding_seats[0])) && boarded < MAXIMUM_BOARDED_SEATS && h2_caps_boarding(); k++)
	{
		if (strncmp(v->name, h2v_boarding_seats[k].collection_prefix, strlen(h2v_boarding_seats[k].collection_prefix)))
			continue;
		for (i = 0; i < count; i++)
		{
			if (!strcmp(seats[i].label, h2v_boarding_seats[k].seat))
			{
				targets[boarded] = (short)i;
				kinds[boarded++] = (short)k;
				break;
			}
		}
	}
	for (i = 0; i < count; i++)
	{
		/* (Halo 2's passenger: third person) */
		if (!strcmp(seats[i].label, "W-passenger") && h2_caps_boarding())
		{
			seats[i].flags |= 1UL << _unit_seat_third_person_camera_bit;
			seats[i].flags &= ~(1UL << _unit_seat_first_person_camera_bit);
		}
	}
	for (i = 0; i < boarded; i++)
	{
		struct unit_seat *b = &seats[count + i];

		*b = seats[targets[i]];
		snprintf(b->label, sizeof(b->label), "%s", h2v_boarding_seats[kinds[i]].label);
		snprintf(b->marker_name, sizeof(b->marker_name), "%s", h2v_boarding_seats[kinds[i]].marker);
		b->flags &= ~((1UL << _unit_seat_driver_bit) | (1UL << _unit_seat_gunner_bit) | (1UL << _unit_seat_first_person_camera_bit));
		b->flags |= 1UL << _unit_seat_third_person_camera_bit;
		b->unused[0] = (long)((h2v_boarding_seats[kinds[i]].beats ? UNIT_SEAT_BEATING_MAGIC : UNIT_SEAT_BOARDING_MAGIC) | (unsigned long)targets[i]);

		/* (Halo 2's boarding seat's maximum relative velocity, a tick's:
		units.c's unit_boarding_allowed) */
		{
			long q;

			b->unused2[0] = b->unused2[1] = 0;
			for (q = 0; q < v->seat_count; q++)
			{
				if (!strcmp(v->seats[q].label, b->label) && v->seats[q].maximum_relative_velocity > 0.0f)
				{
					float per_tick = v->seats[q].maximum_relative_velocity / 30.0f;

					memcpy(&b->unused2[0], &per_tick, sizeof(per_tick));
					b->unused2[1] = (long)UNIT_SEAT_BOARDING_SPEED_MAGIC;
				}
			}
		}
		/* (no seat huds, no built-in actor) */
		b->seat_huds.count = 0;
		b->seat_huds.address = 0;
		b->built_in_actor_reference.index = NONE;
	}
	/* (Halo 2's damage seats: each seat's riders' damage scales, by its
	Halo 2 label - the stand-in's driver, passenger and gunner Halo 2's
	warthog_d, warthog_p, warthog_g, ghost_d; h2_damage.c) */
	for (i = 0; i < count + boarded; i++)
	{
		static char const *const labels[][2] =
		{
			{ "W-driver", "warthog_d" }, { "W-passenger", "warthog_p" }, { "W-gunner", "warthog_g" }, { "G-driver", "ghost_d" },
		};
		char const *label = seats[i].label;
		long l, d;

		for (l = 0; l < (long)(sizeof(labels) / sizeof(labels[0])); l++)
		{
			if (!strcmp(label, labels[l][0]))
				label = labels[l][1];
		}
		if (h2v_kinds[v->kind].seat_names)
		{
			struct h2v_seat_name const *n;

			for (n = h2v_kinds[v->kind].seat_names; n->ce && strcmp(n->ce, seats[i].label); n++)
				;
			if (n->ce)
				label = n->h2;
		}
		seats[i].unused3[0] = 0;
		for (d = 0; d < v->damage_seat_count; d++)
		{
			if (!strcmp(label, v->damage_seats[d].label))
			{
				int q;

				seats[i].unused3[0] = (long)(v->damage_seats[d].child ? H2_DAMAGE_SEAT_MAGIC_CHILD : H2_DAMAGE_SEAT_MAGIC);
				for (q = 0; q < 4; q++)
					memcpy(&seats[i].unused3[1 + q], &v->damage_seats[d].scales[q], sizeof(float));
				if (getenv("HALO_H2_DEBUG"))
					platform_log("h2 vehicles: %s seat %s (%s): damage direct x%.2f, transfer x%.2f..%.2f within %.2f", v->name,
						seats[i].label, label, v->damage_seats[d].scales[0], v->damage_seats[d].scales[3], v->damage_seats[d].scales[2],
						v->damage_seats[d].scales[1]);
			}
		}
	}
	to = region_block(r, field, count + boarded, sizeof(*seats));
	if (!r->b.failed)
		memcpy(region_at(r, to), seats, sizeof(*seats) * (unsigned long)(count + boarded));
	/* (the extra seats' camera tracks their own copies: the tag check
	refuses two blocks over the same elements) */
	for (i = extra_first; extra_first != NONE && i < count && !r->b.failed; i++)
	{
		unsigned long tracks = to + sizeof(*seats) * (unsigned long)i + offsetof(struct unit_seat, camera.unit_camera_tracks);
		long track_count = (long)read32(region_at(r, tracks));
		unsigned long track_from = read32(region_at(r, tracks + 4));

		if (track_count > 0 && track_from)
		{
			unsigned char saved[28 * 4];
			unsigned long copy;

			if (track_count > 4)
				track_count = 4;
			memcpy(saved, region_at(r, track_from - r->link), 28 * (unsigned long)track_count);
			copy = region_block(r, tracks, track_count, 28);
			if (!r->b.failed)
				memcpy(region_at(r, copy), saved, 28 * (unsigned long)track_count);
		}
	}
	/* (own collision: its gunner's seat's camera Halo 2's - its marker,
	pitch range and track) */
	{
		float place[3];

		/* (a camera marker the model lacks - the Spectre's "0" -: the
		stand-in's camera kept) */
		if (track_index != NONE && !h2v_marker_place(&v->model, v->camera_marker, place))
			track_index = NONE;
	}
	if (track_index != NONE && !r->b.failed)
	{
		struct unit_seat *seat = (struct unit_seat *)region_at(r, to);
		unsigned long tracks;

		snprintf(seat->camera.marker_name, sizeof(seat->camera.marker_name), "%s", v->camera_marker);
		if (v->camera_pitch[1] > v->camera_pitch[0])
		{
			seat->camera.pitch_minimum = v->camera_pitch[0];
			seat->camera.pitch_maximum = v->camera_pitch[1];
		}
		tracks = region_block(r, to + offsetof(struct unit_seat, camera.unit_camera_tracks), 1, 28);
		if (!r->b.failed)
			region_reference(r, tracks, 'trak', tag_id(track_index));
	}
	/* (a boarding seat's camera tracks its own copy: the tag check refuses
	two blocks over the same elements) */
	for (i = 0; i < boarded && !r->b.failed; i++)
	{
		unsigned long seat = to + sizeof(*seats) * (unsigned long)(count + i);
		unsigned long tracks_field = seat + offsetof(struct unit_seat, camera.unit_camera_tracks);
		long track_count = (long)read32(region_at(r, tracks_field));
		unsigned long tracks_from = read32(region_at(r, tracks_field + 4));
		unsigned long element = 28, copy;
		unsigned char saved[28 * 8];

		if (track_count <= 0 || track_count > 8 || !tracks_from)
		{
			region_block(r, tracks_field, 0, element);
			continue;
		}
		memcpy(saved, region_at(r, tracks_from - r->link), element * (unsigned long)track_count);
		copy = region_block(r, tracks_field, track_count, element);
		if (!r->b.failed)
			memcpy(region_at(r, copy), saved, element * (unsigned long)track_count);
	}
	/* (every seat's camera Halo 2's seat's - its marker, pitch range and
	track, h2v_seat_track_tag -, by its Halo 2 label, where the model has
	the marker: the Warthog's, Ghost's, Scorpion's, Wraith's, Spectre's,
	Shadow's, turrets' and boarding seats' rode the stand-in's track before,
	the Scorpion driver's 0.9 further and 1.05 higher than Halo 2's) */
	for (i = 0; i < count + boarded && !r->b.failed; i++)
	{
		static char const *const labels[][2] =
		{
			{ "W-driver", "warthog_d" }, { "W-passenger", "warthog_p" }, { "W-gunner", "warthog_g" }, { "G-driver", "ghost_d" },
		};
		unsigned long at = to + sizeof(*seats) * (unsigned long)i, tracks;
		struct unit_seat *seat = (struct unit_seat *)region_at(r, at);
		char const *label = seat->label;
		struct h2v_seat const *h2;
		float place[3];
		long l;

		for (l = 0; l < (long)(sizeof(labels) / sizeof(labels[0])); l++)
		{
			if (!strcmp(label, labels[l][0]))
				label = labels[l][1];
		}
		h2 = h2v_camera_seat(v, NONE, label);
		if (!h2 || h2->track_slot == NONE || (i == 0 && track_index != NONE))
			continue;
		if (h2->camera_marker[0] && !h2v_marker_place(&v->model, h2->camera_marker, place))
		{
			if (getenv("HALO_H2_DEBUG"))
				platform_log("h2 vehicles: %s seat %s: its camera marker '%s' not on the model, the stand-in's camera kept", v->name, label,
					h2->camera_marker);
			continue;
		}
		snprintf(seat->camera.marker_name, sizeof(seat->camera.marker_name), "%s", h2->camera_marker);
		if (h2->camera_pitch[1] > h2->camera_pitch[0])
		{
			seat->camera.pitch_minimum = h2->camera_pitch[0];
			seat->camera.pitch_maximum = h2->camera_pitch[1];
		}
		tracks = region_block(r, at + offsetof(struct unit_seat, camera.unit_camera_tracks), 1, 28);
		if (!r->b.failed)
			region_reference(r, tracks, 'trak', tag_id(first + h2v_seat_track_tag(v, h2->track_slot)));
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 vehicles: %s seat %s (%s): Halo 2's camera '%s', its track", v->name, ((struct unit_seat *)region_at(r, at))->label,
				label, h2->camera_marker);
	}
	if (getenv("HALO_H2_DEBUG"))
		platform_log("h2 vehicles: %s: %ld seats, %ld boarding seats", v->name, count, boarded);
	free(seats);
}

/* (own collision) the collision model object_collision_write made given
the vehicle's nodes: this engine's collision model has a BSP a node (a
node's region's permutation's), so each of Halo 2's BSPs (its first
permutation's) goes on the skeleton's node its Halo 2 node is renamed to,
in that node's space - it moves with it (the turret's gun with its aim) */
H2_ALIGN static boolean h2v_collision_nodes_write(struct build *build, struct region *r, unsigned long root, struct h2v_vehicle const *v)
{
	struct h2_cache *cache = build->cache;
	struct h2v_kind const *k = &h2v_kinds[v->kind];
	struct h2_cache_tag t, model;
	long region_count, node_count = 0, i, n, kept = 0, source;
	unsigned long regions, h2_nodes = 0, nodes;
	boolean has_bsp[H2W_MAXIMUM_NODES];
	char names[H2W_MAXIMUM_NODES][32];
	short parents[H2W_MAXIMUM_NODES];
	long skeleton_count;

	if (!cache)
		return FALSE;

	if (!h2_cache_tag_get(cache, v->h2_collision, &t) || !h2_cache_block(cache, t.address + 0x1C, 0xC, &region_count, &regions) ||
		!h2_cache_tag_get(cache, v->h2_render_model, &model) || !h2_cache_block(cache, model.address + 0x48, 0x60, &node_count, &h2_nodes))
	{
		return FALSE;
	}
	/* (its nodes: its skeleton's, or - its own rig - its model's) */
	skeleton_count = k->own_graph ? v->model.node_count : k->skeleton_count;
	if (skeleton_count > H2W_MAXIMUM_NODES)
		return FALSE;
	for (i = 0; i < skeleton_count; i++)
	{
		snprintf(names[i], sizeof(names[i]), "%s", k->own_graph ? v->model.nodes[i].name : k->skeleton[i].name);
		parents[i] = k->own_graph ? v->model.nodes[i].parent : k->skeleton[i].parent;
	}
	nodes = region_block(r, root + 0x28C, skeleton_count, 64);
	if (r->b.failed)
		return FALSE;
	if (getenv("HALO_H2_DEBUG"))
	{
		for (i = 0; i < skeleton_count; i++)
			platform_log("h2 vehicles: %s: collision node %ld '%s' parent %d", v->name, i, names[i], (int)parents[i]);
	}
	for (i = 0; i < skeleton_count; i++)
	{
		unsigned long node = nodes + 64 * (unsigned long)i;
		long child, sibling;

		snprintf((char *)region_at(r, node), 32, "%s", names[i]);
		write16(region_at(r, node + 0x20), (unsigned short)-1);
		write16(region_at(r, node + 0x22), (unsigned short)parents[i]);
		for (sibling = i + 1; sibling < skeleton_count && parents[sibling] != parents[i]; sibling++)
			;
		for (child = 0; child < skeleton_count && parents[child] != i; child++)
			;
		write16(region_at(r, node + 0x24), (unsigned short)(sibling < skeleton_count ? sibling : -1));
		write16(region_at(r, node + 0x26), (unsigned short)(child < skeleton_count ? child : -1));
		has_bsp[i] = FALSE;
	}
	/* (source 0 the vehicle's own collision model; then - its own rig -
	each child object's, its turret's: the Wraith's mortar, the Spectre's
	plasma gun, whose BSPs go on the turret's nodes, which the driver's aim
	turns, and are the turret's to be hit) */
	for (source = 0; source < H2V_MAXIMUM_PIECES; source++)
	{
	if (source > 0)
	{
		long hlmt = k->own_graph && k->pieces[source].render_model && !getenv("HALO_H2_NO_TURRET_COLLISION") ? h2v_piece_hlmt(cache, k->pieces[source].render_model) : NONE;
		struct h2_cache_tag piece_model;
		long piece_collision, piece_render;

		if (hlmt == NONE)
			break;
		if (!h2_cache_tag_get(cache, hlmt, &piece_model) || (piece_render = h2_cache_reference(cache, piece_model.address + 0x0)) == NONE ||
			(piece_collision = h2_cache_reference(cache, piece_model.address + 0x8)) == NONE ||
			!h2_cache_tag_get(cache, piece_collision, &t) || !h2_cache_block(cache, t.address + 0x1C, 0xC, &region_count, &regions) ||
			!h2_cache_tag_get(cache, piece_render, &model) || !h2_cache_block(cache, model.address + 0x48, 0x60, &node_count, &h2_nodes))
		{
			continue;
		}
	}
	for (i = 0; i < region_count; i++)
	{
		long permutations, bsp_count;
		unsigned long permutation, bsps;

		if (!h2_cache_block(cache, regions + 0xC * (unsigned long)i + 4, 0x14, &permutations, &permutation) || permutations < 1 ||
			!h2_cache_block(cache, permutation + 4, 0x44, &bsp_count, &bsps))
		{
			continue;
		}
		for (n = 0; n < bsp_count; n++)
		{
			unsigned long bsp = bsps + 0x44 * (unsigned long)n;
			short h2_node = (short)read16((unsigned char const *)h2_cache_at(cache, bsp, 2));
			char name[64];
			long q, s2;

			if (h2_node < 0 || h2_node >= node_count)
				continue;
			h2w_name(cache, h2_cache_read32(cache, h2_nodes + 0x60 * (unsigned long)h2_node), name, sizeof(name));
			if (k->own_graph)
			{
				s2 = h2w_model_node(&v->model, name);
				if (s2 < 0)
					s2 = skeleton_count;
				q = 0;
			}
			else
			{
				for (q = 0; k->names[q].h2 && strcmp(k->names[q].h2, name); q++)
					;
				for (s2 = 0; k->names[q].h2 && s2 < skeleton_count && strcmp(k->skeleton[s2].name, k->names[q].ce); s2++)
					;
			}
			/* (a node's second BSP - the Banshee's wing pods, on its hull
			node -: merged into its first, collision_merge) */
			if (s2 < skeleton_count && has_bsp[s2] && (k->own_graph || k->names[q].h2) && !getenv("HALO_H2_NO_COLLISION_MERGE"))
			{
				unsigned long temporary = region_alloc(r, 0x60), field = read32(region_at(r, nodes + 64 * (unsigned long)s2 + 0x34 + 4));

				if (r->b.failed || !field || !collision_convert(build, r, temporary, bsp + 4, 0))
					return FALSE;
				if (collision_merge_tail(r, field - r->link, temporary))
					platform_log("h2 vehicles: %s: collision BSP %ld of node '%s' merged into its first", v->name, n, name);
				else
					platform_log("h2 vehicles: %s: collision BSP %ld of node '%s' left out (it could not be merged)", v->name, n, name);
				continue;
			}
			if ((!k->own_graph && !k->names[q].h2) || s2 >= skeleton_count || has_bsp[s2])
			{
				platform_log("h2 vehicles: %s: collision BSP %ld of node '%s' left out (no node of its own)", v->name, n, name);
				continue;
			}
			{
				unsigned long out = region_block(r, nodes + 64 * (unsigned long)s2 + 0x34, 1, 0x60);

				if (r->b.failed || !collision_convert(build, r, out, bsp + 4, 0))
					return FALSE;
			}
			write16(region_at(r, nodes + 64 * (unsigned long)s2 + 0x20), 0);
			has_bsp[s2] = TRUE;
			kept++;
			if (source > 0)
				platform_log("h2 vehicles: %s: its child object %ld's collision BSP of node '%s' on its node %ld", v->name, source, name, s2);
		}
	}
	}
	platform_log("h2 vehicles: %s: its collision model's %ld BSPs on its nodes", v->name, kept);
	return !r->b.failed && kept > 0;
}

/* the stand-in graph's animations named so as Halo 2's (the end of the
name of one of the vehicle's: "<piece>:<Halo 2's name>") */
static struct
{
	char const *stand_in;
	char const *h2;
} const h2v_graph_names[] =
{
	{ "vehicle steering", ":vehicle:steering" },
	{ "vehicle velocity", ":vehicle:velocity" },
	{ "stand fixed aim-still", ":combat:fixed:aim_still_up" },
	/* (Halo 3's mounted turrets' aim: "any:any:aim_still_up") */
	{ "stand fixed aim-still", ":any:any:aim_still_up" },
	{ "firing", ":fire_1" },
	{ "firing", ":firing" },
	{ "stand acc-front-back", ":combat:acc_front_back" },
	{ "stand acc-up-down", ":combat:acc_up_down" },
	{ "suspension left back", ":suspension:left_back" },
	{ "suspension right back", ":suspension:right_back" },
	{ "suspension left front", ":suspension:left_front" },
	{ "suspension right front", ":suspension:right_front" },
};

/* the vehicle's animation whose name ends so, NONE none */
H2_ALIGN static short h2v_graph_find(struct h2w_graph const *graph, char const *end)
{
	long i;
	size_t length = strlen(end);

	for (i = 0; i < graph->animation_count; i++)
	{
		size_t name_length = strlen(graph->animations[i].name);

		if (name_length >= length && !strcmp(graph->animations[i].name + name_length - length, end))
			return (short)i;
	}
	return NONE;
}

/* a block of animation indices (shorts) remapped, old to new */
H2_ALIGN static void h2v_graph_remap_indices(struct region *r, unsigned long field, short const *map, long old_count)
{
	long count = (long)read32(region_at(r, field)), i;
	unsigned long elements = read32(region_at(r, field + 4));

	for (i = 0; i < count && elements; i++)
	{
		unsigned long at = elements - r->link + 2 * (unsigned long)i;
		short old = (short)read16(region_at(r, at));

		write16(region_at(r, at), (unsigned short)(old >= 0 && old < old_count ? map[old] : NONE));
	}
}

/* (own graph) the vehicle's graph tag (at index): the stand-in's (its unit
seats, weapon classes and vehicle block: what plays when), its nodes the
rig's and its animations Halo 2's; every index of the stand-in's to the
animation of Halo 2's of that use (h2v_graph_names), none if none; the
vehicle block's steering (0) and velocity (3) Halo 2's, the aiming bounds
Halo 2's blend screens' */
H2_ALIGN static void h2v_graph_write_from(struct region *r, unsigned long instances, unsigned long vehicle_root, long base, long index,
	struct h2v_vehicle const *v);

H2_ALIGN static void h2v_graph_write(struct region *r, unsigned long instances, unsigned long vehicle_root, long index, struct h2v_vehicle const *v)
{
	long base = (short)read16(region_at(r, vehicle_root + offsetof(struct unit_definition, object.animation_graph) + 12));

	h2v_graph_write_from(r, instances, vehicle_root, base, index, v);
}

/* (the stand-in's graph the tag base: the vehicle's own, before it is given
its graph - or, at load (h2_models_load.inc), the kit vehicle's) */
H2_ALIGN static void h2v_graph_write_from(struct region *r, unsigned long instances, unsigned long vehicle_root, long base, long index,
	struct h2v_vehicle const *v)
{
	unsigned long base_root = base >= 0 ? h2w_root(r, instances, base) : 0, root, field, elements;
	short map[MAXIMUM_ANIMATIONS_PER_GRAPH];
	long old_count, i, count, mapped = 0;
	short aim = h2v_graph_find(v->graph, ":combat:fixed:aim_still_up");
	short steering = h2v_graph_find(v->graph, ":vehicle:steering");
	short velocity = h2v_graph_find(v->graph, ":vehicle:velocity");

	if (!base_root)
		return;
	root = h2w_tag_copy(r, base_root, sizeof(struct animation_graph), 'antr');
	if (r->b.failed)
		return;
	write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 20), r->link + root);
	/* (the stand-in's names, before its animations are replaced) */
	old_count = (long)read32(region_at(r, root + offsetof(struct animation_graph, animations)));
	elements = read32(region_at(r, root + offsetof(struct animation_graph, animations) + 4));
	if (old_count > MAXIMUM_ANIMATIONS_PER_GRAPH)
		old_count = MAXIMUM_ANIMATIONS_PER_GRAPH;
	for (i = 0; i < old_count; i++)
	{
		char const *name = elements ? (char const *)region_at(r, elements - r->link + sizeof(struct animation) * (unsigned long)i) : "";
		long k;

		map[i] = NONE;
		for (k = 0; k < (long)(sizeof(h2v_graph_names) / sizeof(h2v_graph_names[0])) && map[i] == NONE; k++)
		{
			/* (Halo 3's turrets' name: a Halo 3 kind's alone) */
			if (!strcmp(h2v_graph_names[k].h2, ":any:any:aim_still_up") && !h2v_kinds[v->kind].h3)
				continue;
			if (!strcmp(name, h2v_graph_names[k].stand_in))
				map[i] = h2v_graph_find(v->graph, h2v_graph_names[k].h2);
		}
		/* (a gun's firing: "... fire-1", "... fire-2") */
		if (map[i] == NONE && strstr(name, "fire-1"))
		{
			map[i] = h2v_graph_find(v->graph, ":fire_1");
			if (map[i] == NONE)
				map[i] = h2v_graph_find(v->graph, ":firing");
		}
		if (map[i] == NONE && strstr(name, "fire-2"))
			map[i] = h2v_graph_find(v->graph, ":fire_2");
		mapped += map[i] != NONE;
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 vehicles: %s graph: the stand-in's '%s' -> %d", v->name, name, (int)map[i]);
	}
	h2w_graph_write(r, root, v->graph, NONE);
	region_block(r, root + offsetof(struct animation_graph, first_person_weapon_animations), 0, 0x1C);
	region_block(r, root + offsetof(struct animation_graph, weapon_animations), 0, 4);
	region_block(r, root + offsetof(struct animation_graph, device_animations), 0, 4);
	region_block(r, root + offsetof(struct animation_graph, unit_damage_animations), 0, 4);
	/* (object overlays: their animations) */
	field = root + offsetof(struct animation_graph, object_overlays);
	count = (long)read32(region_at(r, field));
	elements = read32(region_at(r, field + 4));
	for (i = 0; i < count && elements; i++)
	{
		unsigned long at = elements - r->link + sizeof(struct animation_graph_object_overlay) * (unsigned long)i;
		short old = (short)read16(region_at(r, at));

		write16(region_at(r, at), (unsigned short)(old >= 0 && old < old_count ? map[old] : NONE));
	}
	/* (and its hatch's, played by the hatch function: h2_vehicles_write) */
	if (v->hatch_count)
	{
		unsigned long from = count && elements ? elements - r->link : 0, to;
		long h;

		to = region_block(r, field, count + v->hatch_count, sizeof(struct animation_graph_object_overlay));
		if (r->b.failed)
			return;
		if (from)
			memcpy(region_at(r, to), region_at(r, from), (size_t)count * sizeof(struct animation_graph_object_overlay));
		for (h = 0; h < v->hatch_count; h++)
		{
			unsigned long at = to + sizeof(struct animation_graph_object_overlay) * (unsigned long)(count + h);

			memset(region_at(r, at), 0, sizeof(struct animation_graph_object_overlay));
			write16(region_at(r, at + offsetof(struct animation_graph_object_overlay, animation_index)), (unsigned short)v->hatches[h]);
			write16(region_at(r, at + offsetof(struct animation_graph_object_overlay, function_index)), H2V_HATCH_FUNCTION);
			write16(region_at(r, at + offsetof(struct animation_graph_object_overlay, mode)), _object_overlay_mode_frame);
		}
	}
	/* (unit seats, their weapon classes and types) */
	field = root + offsetof(struct animation_graph, unit_seats);
	count = (long)read32(region_at(r, field));
	elements = read32(region_at(r, field + 4));
	for (i = 0; i < count && elements; i++)
	{
		unsigned long seat = elements - r->link + sizeof(struct animation_graph_unit_seat) * (unsigned long)i;
		unsigned long classes;
		long class_count, c;

		h2v_graph_remap_indices(r, seat + offsetof(struct animation_graph_unit_seat, animations), map, old_count);
		class_count = (long)read32(region_at(r, seat + offsetof(struct animation_graph_unit_seat, weapon_classes)));
		classes = read32(region_at(r, seat + offsetof(struct animation_graph_unit_seat, weapon_classes) + 4));
		for (c = 0; c < class_count && classes; c++)
		{
			unsigned long wc = classes - r->link + sizeof(struct animation_graph_weapon_class) * (unsigned long)c;
			unsigned long types;
			long type_count, t;

			h2v_graph_remap_indices(r, wc + offsetof(struct animation_graph_weapon_class, animations), map, old_count);
			/* (its aiming: Halo 2's aim screen's bounds) */
			if (aim != NONE && v->has_screen[aim])
				memcpy(region_at(r, wc + offsetof(struct animation_graph_weapon_class, aiming_screen_bounds)), v->screens[aim], 0x18);
			type_count = (long)read32(region_at(r, wc + offsetof(struct animation_graph_weapon_class, weapon_types)));
			types = read32(region_at(r, wc + offsetof(struct animation_graph_weapon_class, weapon_types) + 4));
			for (t = 0; t < type_count && types; t++)
				h2v_graph_remap_indices(r, types - r->link + sizeof(struct animation_graph_weapon_type) * (unsigned long)t +
					offsetof(struct animation_graph_weapon_type, animations), map, old_count);
		}
	}
	/* (the vehicle block: its animations - steering 0, roll 1, throttle 2,
	velocity 3, braking 4, ground speed 5 - and suspensions) */
	field = root + offsetof(struct animation_graph, vehicle_animations);
	count = (long)read32(region_at(r, field));
	elements = read32(region_at(r, field + 4));
	if (count > 0 && elements)
	{
		unsigned long block = elements - r->link;
		unsigned long slots = block + 0x5C;
		long slot_count = (long)read32(region_at(r, slots));
		unsigned long suspensions = read32(region_at(r, block + 0x68));
		long suspension_count = (long)read32(region_at(r, block + 0x68));
		short values[8];

		suspensions = read32(region_at(r, block + 0x68 + 4));
		for (i = 0; i < 8; i++)
		{
			values[i] = NONE;
			if (i < slot_count && read32(region_at(r, slots + 4)))
			{
				short old = (short)read16(region_at(r, read32(region_at(r, slots + 4)) - r->link + 2 * (unsigned long)i));

				values[i] = old >= 0 && old < old_count ? map[old] : NONE;
			}
		}
		values[0] = steering;
		values[3] = velocity;
		{
			unsigned long to = region_block(r, slots, 8, 2);

			for (i = 0; i < 8 && !r->b.failed; i++)
				write16(region_at(r, to + 2 * (unsigned long)i), (unsigned short)values[i]);
		}
		if (steering != NONE && v->has_screen[steering])
			memcpy(region_at(r, block), v->screens[steering], 0x18);
		for (i = 0; i < suspension_count && suspensions && !r->b.failed; i++)
		{
			unsigned long at = suspensions - r->link + 0x14 * (unsigned long)i + 2;
			short old = (short)read16(region_at(r, at));

			write16(region_at(r, at), (unsigned short)(old >= 0 && old < old_count ? map[old] : NONE));
		}
	}
	region_reference(r, vehicle_root + offsetof(struct unit_definition, object.animation_graph), 'antr', tag_id(index));
	platform_log("h2 vehicles: %s: its graph written: %ld animations, %ld of the stand-in's uses given Halo 2's (steering %d, velocity %d, aim %d)",
		v->name, v->graph->animation_count, mapped, (int)steering, (int)velocity, (int)aim);
}

/* the vehicles' tags (first: the vehicles' first tag index) */

/* (h2_vehicles_write) an object function passing an input out as it is
(scale_function_by_function_index: 1-4 A-D in, 0 nothing) */
H2_ALIGN static void h2v_function_pass(struct region *r, unsigned long f, short input)
{
	memset(region_at(r, f), 0, sizeof(struct object_function_definition));
	write16(region_at(r, f + offsetof(struct object_function_definition, scale_function_by_function_index)), (unsigned short)input);
	writef(region_at(r, f + offsetof(struct object_function_definition, upper_bound)), 1.0f);
	write16(region_at(r, f + offsetof(struct object_function_definition, turn_off_with_function_index)), (unsigned short)-1);
	writef(region_at(r, f + offsetof(struct object_function_definition, runtime_reciprocal_bounds_range)), 1.0f);
	writef(region_at(r, f + offsetof(struct object_function_definition, runtime_one_over_period)), 1.0f);
}

/* a made vehicle's own values Halo 2's (h2v_vehicle_values_read): its
driving, flip back, slide, acceleration scale, splatter and motion sensor
blip - written as a level is made and again as one is loaded
(h2_weapon_fields_map_loaded), the same writes */
H2_ALIGN static void h2v_root_values_write(struct region *r, unsigned long root, struct h2v_vehicle const *v)
{
	/* Halo 2's driving: speeds a tick's, accelerations a tick's a tick
	(a hover vehicle's turns are its aim's: only its speeds) */
	if (v->has_driving)
	{
		short type = (short)read16(region_at(r, root + 0x2F4));
		int q;

		writef(region_at(r, root + 0x2F8), v->driving[0] / 30.0f);
		writef(region_at(r, root + 0x2FC), v->driving[1] / 30.0f);
		writef(region_at(r, root + 0x300), v->driving[2] / 900.0f);
		writef(region_at(r, root + 0x304), v->driving[3] / 900.0f);
		if (type == 1)  /* (a jeep's) */
		{
			for (q = 4; q < 8; q++)
				writef(region_at(r, root + 0x2F8 + 4 * q), v->driving[q]);
			writef(region_at(r, root + 0x318), v->driving[8] / 30.0f);
		}
		/* (a hover vehicle's sliding - a Ghost's strafe -: Halo 2's
		maximum left and right slide a tick's, its acceleration and
		deceleration a tick's a tick; vehicles.c strafes by them on a
		Halo 2 level) */
		/* (its flip back over: Halo 2's spin bounds a tick's, vehicles.c
		ef870's rule - the stand-in's, spun for two seconds on a Halo 2
		level) */
		if (v->flip[1] > v->flip[0] && v->flip[0] > 0.0f)
		{
			writef(region_at(r, root + 0x340), v->flip[0] / 30.0f);
			writef(region_at(r, root + 0x344), v->flip[1] / 30.0f);
		}
		if (type == 4 && v->slide[0] > 0.0f)
		{
			writef(region_at(r, root + 0x330), v->slide[0] / 30.0f);
			writef(region_at(r, root + 0x334), v->slide[1] / 30.0f);
			writef(region_at(r, root + 0x338), v->slide[2] / 900.0f);
			writef(region_at(r, root + 0x33C), v->slide[3] / 900.0f);
		}
	}
	/* (splatter, physics.c on a Halo 2 level: its collision damage
	transfer scale and the players' collision damage limits - their
	minimum and maximum force and damage scale -, Halo 2's, in the
	vehicle definition's unused 0x348) */
	/* (its acceleration scale Halo 2's, not the stand-in's: the
	pushes of damage and of its death's effects, damage.c and
	h2_vehicle_physics.c, times it - the Scorpion 0.1, the Warthog
	0.75, the Ghost 1, the Banshee 1.25) */
	if (v->acceleration_scale >= 0.0f && v->acceleration_scale < 100.0f)
	{
		if (getenv("HALO_H2_DEBUG"))
			platform_log("h2 vehicles: %s: acceleration scale %.3f (the stand-in's %.3f)", v->name, v->acceleration_scale,
				readf(region_at(r, root + offsetof(struct unit_definition, object.acceleration_scale))));
		writef(region_at(r, root + offsetof(struct unit_definition, object.acceleration_scale)), v->acceleration_scale);
	}
	if (v->collision_transfer_scale > 0.0f && h2v.splatter[1] > h2v.splatter[0])
	{
		int q;

		write32(region_at(r, root + 0x348), H2_VEHICLE_SPLATTER_MAGIC);
		writef(region_at(r, root + 0x34C), v->collision_transfer_scale);
		for (q = 0; q < 4; q++)
			writef(region_at(r, root + 0x350 + 4 * q), h2v.splatter[q]);
	}
	/* Halo 2's motion sensor blip size (large for a Warthog or Ghost, small
	for the gun Warthogs) */
	if (v->blip_size >= 0)
		write16(region_at(r, root + offsetof(struct unit_definition, unit.blip_type)), (unsigned short)v->blip_size);
}

/* a vehicle's collision model's damage, Halo 2's (coll_root: an offset of r):
its damaged and destroyed effects the kit's smoke and fire explosion (the
effects pass gives them Halo 2's after), its body vitality, shield,
regeneration, materials - as the level is made, and as a collision model
is made at load (h2_models_load.inc) */
H2_ALIGN static void h2v_collision_damage_write(struct region *r, unsigned long instances, unsigned long coll_root, struct h2v_vehicle const *v)
{
	if (!coll_root || getenv("HALO_H2_VEHICLE_DAMAGE_AS_IS"))
		return;
	if (h2v.kit_smoke != NONE)
	{
		region_reference(r, coll_root + offsetof(struct damage_resistance, body_damaged_effect), 'effe', h2v_instance_id(r, instances, h2v.kit_smoke));
		writef(region_at(r, coll_root + offsetof(struct damage_resistance, body_damaged_effect_threshold)), 0.5f);
	}
	if (h2v.kit_fire_explosion != NONE)
		region_reference(r, coll_root + offsetof(struct damage_resistance, body_depleted_effect), 'effe', h2v_instance_id(r, instances, h2v.kit_fire_explosion));
	/* (Halo 2's vehicles die: their vitality Halo 2's, as the
	players' and the weapons' damage are, hurt by any weapon) */
	if (v->vitality > 0.0f)
	{
		writef(region_at(r, coll_root + offsetof(struct damage_resistance, maximum_body_vitality)), v->vitality);
		/* (Halo 2's vehicles' bodies come back a little: after
		its stun time undamaged, up to its recharge fraction over
		its recharge time - h2_damage.c, as a player's body) */
		/* (a shield of Halo 2's - the Covenant turret's: an energy
		shield, the Elite's material type's multipliers) */
		if (v->shield[0] > 0.0f && !getenv("HALO_H2_NO_VEHICLE_SHIELDS"))
		{
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, maximum_shield_vitality)), v->shield[0]);
			write16(region_at(r, coll_root + offsetof(struct damage_resistance, shield_material_type)), _material_elite_energy_shield);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, minimum_shield_stun_damage)), v->shield[1]);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, shield_stun_time)), v->shield[2]);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, shield_recharge_time)), v->shield[3]);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, runtime_shield_recharge_velocity)),
				v->shield[3] > 0.0f ? 1.0f / (v->shield[3] * 30.0f) : 0.0f);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, shield_failure_threshold)), 0.0f);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, maximum_shield_failure)), 0.0f);
			platform_log("h2 vehicles: %s: its shield %.0f (stun %.1f s, recharge %.1f s)", v->name, v->shield[0], v->shield[2], v->shield[3]);
		}
		if (!getenv("HALO_H2_NO_VEHICLE_REGENERATION"))
		{
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, port_body_minimum_stun_damage)), v->body_regeneration[0]);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, port_body_stun_time)), v->body_regeneration[1]);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, port_body_recharge_velocity)),
				v->body_regeneration[2] > 0.0f ? 1.0f / (v->body_regeneration[2] * 30.0f) : 0.0f);
			writef(region_at(r, coll_root + offsetof(struct damage_resistance, port_body_recharge_fraction)), v->body_regeneration[3]);
			platform_log("h2 vehicles: %s: its body back to %.3f over %.1f s after %.1f s undamaged", v->name, v->body_regeneration[3],
				v->body_regeneration[2], v->body_regeneration[1]);
		}
		write32(region_at(r, coll_root + offsetof(struct damage_resistance, flags)),
			read32(region_at(r, coll_root + offsetof(struct damage_resistance, flags))) & ~(1UL << 5));
		/* (the stand-in's materials take no body damage: they do) */
		{
			long m, count = (long)read32(region_at(r, coll_root + offsetof(struct damage_resistance, materials)));
			unsigned long materials = read32(region_at(r, coll_root + offsetof(struct damage_resistance, materials) + 4));

			for (m = 0; m < count && materials; m++)
			{
				unsigned long at = materials - r->link + sizeof(struct damage_resistance_material) * m +
					offsetof(struct damage_resistance_material, body_damage_multiplier);

				if (readf(region_at(r, at)) <= 0.0f)
					writef(region_at(r, at), 1.0f);
			}
			/* (its armor Halo 2's: every material the vehicle's
			material type, whose modifiers the damage effects made
			from Halo 2's have the damage table's for, h2_damage.c) */
			if (h2_damage_vehicle_material_type(v->name) != NONE && !getenv("HALO_H2_DAMAGE_AS_IS"))
			{
				for (m = 0; m < count && materials; m++)
					write16(region_at(r, materials - r->link + sizeof(struct damage_resistance_material) * m +
						offsetof(struct damage_resistance_material, material_type)), h2_damage_vehicle_material_type(v->name));
			}
			/* (area damage - explosions - takes the indirect
			material: none was named, which takes no body damage) */
			if (count > 0 && (short)read16(region_at(r, coll_root + offsetof(struct damage_resistance, indirect_damage_material_index))) < 0)
				write16(region_at(r, coll_root + offsetof(struct damage_resistance, indirect_damage_material_index)), 0);
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 vehicles: %s collision materials %ld\n", v->name, count);
		}
	}
}

H2_ALIGN static void h2_vehicles_write(struct build *build, struct region *r, unsigned long instances, long first, long model_first)
{
	long i;

	h2v_seat_text_gunner = h2v_seat_text_passenger = NONE;

	for (i = 0; i < h2v.count && !r->b.failed; i++)
	{
		struct h2v_vehicle *v = h2v.vehicles[i];
		long index = first + v->tag, gun = NONE;
		unsigned long base_root = v->base_vehicle != NONE ? h2w_root(r, instances, v->base_vehicle) : 0;
		unsigned long root, gun_root = 0, projectile_root = 0, damage_root = 0;

		h2w_model_write(r, h2w_root(r, instances, model_first + v->model.model), &v->model);
		/* its force model's values (h2_vehicle_physics.h): a string list of
		one string, the values' bytes */
		{
			long physics_index = first + h2v_physics_tag(v);
			unsigned long list = region_alloc(r, 12), entry;

			if (r->b.failed)
				break;
			/* (and its damage definition packed, the second string: a level
			read back from its cached file has it, h2_vehicle_damage.c) */
			static unsigned char packed[sizeof(struct h2vd_definition) + sizeof(struct h2vd_definition) / 2 + 64];
			long packed_size = v->damage ? h2_vehicle_damage_pack(v->damage, packed, (long)sizeof(packed)) : 0;

			entry = region_block(r, list, packed_size > 0 ? 2 : 1, 20);
			if (r->b.failed)
				break;
			region_data(r, entry, &v->physics, sizeof(v->physics));
			if (packed_size > 0)
				region_data(r, entry + 20, packed, packed_size);
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * physics_index + 20), r->link + list);
		}
		/* (own collision) Halo 2's collision model made this engine's as a
		scenery object's is (object_collision_write: its first permutation's
		BSPs of each region, those of nodes turned from the object's frame
		left out), and its seat's camera track - Halo 2's control points,
		this engine's layout's own (a position and an orientation each) */
		if (h2v_own_collision(v))
		{
			long collision_index = first + h2v_physics_tag(v) + 1, track_index = collision_index + 1, n;
			unsigned long collision = region_alloc(r, 664), track, points;
			struct object_made made;

			memset(&made, 0, sizeof(made));
			made.h2_tag = NONE;
			made.model = NONE;
			made.collision = v->h2_collision;
			made.render_model = v->h2_render_model;
			made.vitality = 1.0f;
			snprintf(made.name, sizeof(made.name), "%s", v->name);
			if (r->b.failed)
				break;
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * collision_index + 20), r->link + collision);
			/* (at load, h2_models_load.inc: a stub here - its root with no
			effects, as object_collision_write leaves them, for the effects
			pass to read and write -, the whole one made from the map as the
			level loads) */
			if (h2w_collision_at_load)
				object_collision_stub_write(r, collision);
			else if (!object_collision_write(build, r, collision, &made) || !h2v_collision_nodes_write(build, r, collision, v))
				platform_log("h2 vehicles: %s: its collision model could not be made", v->name);
			track = region_alloc(r, 0x30);
			if (r->b.failed)
				break;
			points = region_block(r, track + 4, v->camera_point_count, 0x3C);
			for (n = 0; n < v->camera_point_count && !r->b.failed; n++)
			{
				int q;

				for (q = 0; q < 7; q++)
					writef(region_at(r, points + 0x3C * (unsigned long)n + 4 * (unsigned long)q), v->camera_points[n][q]);
			}
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * track_index + 20), r->link + track);
		}
		/* (its seats' camera tracks, Halo 2's, as the own seat's above) */
		{
			long slot;

			for (slot = 0; slot < v->seat_track_count && !r->b.failed; slot++)
			{
				struct h2v_seat const *seat = h2v_camera_seat(v, slot, NULL);
				unsigned long track, points;
				long n;

				track = region_alloc(r, 0x30);
				if (r->b.failed)
					break;
				points = region_block(r, track + 4, seat ? seat->camera_point_count : 0, 0x3C);
				for (n = 0; seat && n < seat->camera_point_count && !r->b.failed; n++)
				{
					int c;

					for (c = 0; c < 7; c++)
						writef(region_at(r, points + 0x3C * (unsigned long)n + 4 * (unsigned long)c), seat->camera_points[n][c]);
				}
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2v_seat_track_tag(v, slot)) + 20), r->link + track);
			}
		}
		if (!base_root)
		{
			platform_log("h2 vehicles: %s: the kit has no %s", v->name, h2v_kinds[v->kind].base);
			continue;
		}
		root = h2w_tag_copy(r, base_root, 0x3F0, 'vehi');
		if (r->b.failed)
			break;
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 20), r->link + root);
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 4), 'unit');
		write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * index + 8), 'obje');
		region_reference(r, root + offsetof(struct unit_definition, object.model), 'mode', tag_id(model_first + v->model.model));
		/* (what locks on it: h2_projectiles.c) */
		if (v->lock.tag)
			memcpy(region_at(r, root + offsetof(struct unit_definition, object.unused2) + 4 * H2_OBJECT_PORT_WORD), &v->lock, sizeof(v->lock));
		/* (the stand-in's own hover dust, a Halo CE effect made at the ground
		under each thruster (vehicles.c, create_ghost_effect): a grey haze round
		every driven Ghost, none in MCC's frames. Halo 2's glow under it is
		its own attachment, antigravity_particles1 by driver_seat_power) */
		region_reference(r, root + 0x3E0, 'effe', 0xFFFFFFFFUL);
		/* the Ghost's glow panels (its plasma vent shader) follow its boost: Halo 2
		lights them only while it boosts. The vehicle's third function input (C) is
		the boost power (vehicles.c: mode 13, the one the enum leaves free), a third function
		passes it out (c_out) beside the kit's two, and the shader fades by c_out
		(h2_level.c); HALO_H2_GHOST_GLOW_ALWAYS keeps the old steady glow */
		if (getenv("HALO_H2_DEBUG_FUNCTIONS"))
		{
			short const *um = (short const *)region_at(r, root + offsetof(struct unit_definition, unit.function_modes));
			short const *om = (short const *)region_at(r, root + offsetof(struct unit_definition, object.function_modes));

			platform_log("h2 vehicles: %s (%s): object in %d %d %d %d, unit in %d %d %d %d, vehicle in %d %d %d %d, %ld functions", v->name,
				h2v_kinds[v->kind].base, om[0], om[1], om[2], om[3], um[0], um[1], um[2], um[3],
				read16(region_at(r, root + 0x31C)), read16(region_at(r, root + 0x31E)), read16(region_at(r, root + 0x320)),
				read16(region_at(r, root + 0x322)), (long)read32(region_at(r, root + offsetof(struct unit_definition, object.functions))));
			{
				long count = (long)read32(region_at(r, root + offsetof(struct unit_definition, object.functions))), q;
				unsigned long old = read32(region_at(r, root + offsetof(struct unit_definition, object.functions) + 4));

				for (q = 0; q < count && old; q++)
				{
					struct object_function_definition const *f = (struct object_function_definition const *)region_at(r, old - r->link +
						(unsigned long)q * sizeof(struct object_function_definition));

					platform_log("h2 vehicles: %s: function %ld: flags %lx type %d input %d period %.2f bounds %.2f %.2f usage %s", v->name, q,
						f->flags, f->function_type, f->scale_function_by_function_index, f->period, f->lower_bound, f->upper_bound, f->usage);
				}
			}
		}
		if ((!strncmp(v->name, "ghost", 5) || v->physics.boost_peak_power > 1.0f) && !getenv("HALO_H2_GHOST_GLOW_ALWAYS"))
		{
			unsigned long old = read32(region_at(r, root + offsetof(struct unit_definition, object.functions) + 4));
			long count = (long)read32(region_at(r, root + offsetof(struct unit_definition, object.functions)));

			if (count == 2 && old && read16(region_at(r, root + 0x320)) == 0)
			{
				unsigned long from = old - r->link;
				unsigned long to = region_block(r, root + offsetof(struct unit_definition, object.functions), 3,
					sizeof(struct object_function_definition));

				if (r->b.failed)
					break;
				memcpy(region_at(r, to), region_at(r, from), 2 * sizeof(struct object_function_definition));
				memcpy(region_at(r, to + 2 * sizeof(struct object_function_definition)), region_at(r, from), sizeof(struct object_function_definition));
				h2v_function_pass(r, to + 2 * sizeof(struct object_function_definition), 3);
				write16(region_at(r, root + 0x320), 13);        /* (vehicle function C in: the boost, vehicles.c) */
			}
		}
		/* (its change colours, the Banshee's plates - a change colour shader,
		its body purple by primary, its pod by secondary: Halo 2's object
		tag's by variant, C and D as a biped's (h2_biped.c), A and B white.
		None, the plates were the base map's bare white: tan and pink in the
		sun. Human vehicles keep the stand-in's) */
		if (build->cache && h2v_kinds[v->kind].placed_only && strncmp(v->name, "warthog", 7) && strncmp(v->name, "scorpion", 8))
		{
			struct h2_change_colours *cc = malloc(sizeof(*cc));
			long tag = h2_cache_tag_find(build->cache, 'vehi', h2v_kinds[v->kind].pieces[0].render_model);

			if (cc && h2_change_colours_read(build->cache, tag, cc) && cc->count[2] > 0)
			{
				int slots = cc->count[3] ? 4 : 3, k;
				unsigned long colours = region_block(r, root + offsetof(struct unit_definition, object.change_colors), slots, 0x2C);

				for (k = 0; k < slots && !r->b.failed; k++)
				{
					long count = k >= 2 ? cc->count[k] : 1, p;
					unsigned long permutations = region_block(r, colours + 0x2C * (unsigned long)k + 0x20, count, 0x1C);
					int j;

					for (j = 0; j < 3 && !r->b.failed; j++)
					{
						writef(region_at(r, colours + 0x2C * (unsigned long)k + 8 + 4 * (unsigned long)j), 1.0f);
						writef(region_at(r, colours + 0x2C * (unsigned long)k + 0x14 + 4 * (unsigned long)j), 1.0f);
					}
					for (p = 0; p < count && !r->b.failed; p++)
					{
						unsigned char *o = region_at(r, permutations + 0x1C * (unsigned long)p);

						writef(o, k >= 2 ? cc->permutations[k][p].weight : 1.0f);
						for (j = 0; j < 3; j++)
						{
							writef(o + 4 + 4 * j, k >= 2 ? cc->permutations[k][p].lower[j] : 1.0f);
							writef(o + 0x10 + 4 * j, k >= 2 ? cc->permutations[k][p].upper[j] : 1.0f);
						}
					}
				}
				platform_log("h2 vehicles: %s: change colours C %d, D %d permutations", v->name, (int)cc->count[2], (int)cc->count[3]);
			}
			free(cc);
		}
		/* (the Scorpion stand-in's own headlight, by C: off with Halo 2's where
		flashlights_allowed is, h2_level.c) */
		if (!strncmp(v->name, "scorpion", 8))
		{
			extern boolean h2_vehicle_function_flashlight_scaled(struct build *build, char const *function_name);

			if (h2_vehicle_function_flashlight_scaled(build, "headlight"))
				write16(region_at(r, root + offsetof(struct unit_definition, unit.function_modes) + 2 * 2), 0);
		}
		/* the Spectre's boost (its vents, pods and light by "boost"): its Warthog
		stand-in's C is the speed its engine sound is scaled by, its B the tail
		lights its own Halo 2 lights replace: B the boost (vehicles.c: mode 13);
		h2_effects.c's boost attachments on a Warthog-based vehicle read B */
		if (!strncmp(v->name, "spectre", 7) && v->physics.boost_peak_power > 1.0f)
			write16(region_at(r, root + 0x31E), 13);
		/* its hatch or canopy (Halo 2 opens it as a rider gets in or out): the
		vehicle's fourth function input (D) is how open it is (vehicles.c:
		mode 37, h2_vehicle_hatch_value), a fourth function passes it out
		(d_out, the stand-in's functions padded with ones passing nothing)
		and the graph's hatch overlays play by it (h2v_graph_write) */
		if (v->hatch_count && read16(region_at(r, root + 0x322)) == 0)
		{
			unsigned long old = read32(region_at(r, root + offsetof(struct unit_definition, object.functions) + 4));
			long count = (long)read32(region_at(r, root + offsetof(struct unit_definition, object.functions))), i;

			if (count <= H2V_HATCH_FUNCTION && (!count || old))
			{
				unsigned long from = count ? old - r->link : 0;
				unsigned long to = region_block(r, root + offsetof(struct unit_definition, object.functions), H2V_HATCH_FUNCTION + 1,
					sizeof(struct object_function_definition));

				if (r->b.failed)
					break;
				if (count)
					memcpy(region_at(r, to), region_at(r, from), (size_t)count * sizeof(struct object_function_definition));
				for (i = count; i <= H2V_HATCH_FUNCTION; i++)
					h2v_function_pass(r, to + (unsigned long)i * sizeof(struct object_function_definition), i == H2V_HATCH_FUNCTION ? 4 : 0);
				write16(region_at(r, root + 0x322), 37);        /* (vehicle function D in: the hatch, vehicles.c) */
			}
			/* (a stand-in whose fourth function already passes D in out -
			the Spectre's, D in none: 0 always - has it pass the hatch) */
			else if (count == H2V_HATCH_FUNCTION + 1 && old &&
				read16(region_at(r, old - r->link + H2V_HATCH_FUNCTION * sizeof(struct object_function_definition) +
				offsetof(struct object_function_definition, scale_function_by_function_index))) == 4)
			{
				h2v_function_pass(r, old - r->link + H2V_HATCH_FUNCTION * sizeof(struct object_function_definition), 4);
				write16(region_at(r, root + 0x322), 37);        /* (vehicle function D in: the hatch, vehicles.c) */
			}
			else
			{
				platform_log("h2 vehicles: %s: no room for its hatch's function (%ld functions)", v->name, count);
				v->hatch_count = 0;     /* (its graph has no hatch overlays then) */
			}
		}
		/* (its seats: Halo 2's boarding, h2_caps_boarding, and its riders'
		damage scales) */
		/* (the stand-in Warthog's gunner's and passenger's seat names - the
		prompt's "gunner seat", "passenger seat" -, for the extra seats of
		the vehicles after it: the Shadow's) */
		if (!strcmp(h2v_kinds[v->kind].base, "vehicles\\warthog\\warthog"))
		{
			long seat_count = (long)read32(region_at(r, root + offsetof(struct unit_definition, unit.seats)));
			unsigned long seat_at = read32(region_at(r, root + offsetof(struct unit_definition, unit.seats) + 4));
			long q;

			for (q = 0; q < seat_count && seat_at; q++)
			{
				struct unit_seat const *seat = (struct unit_seat const *)region_at(r, seat_at - r->link + sizeof(struct unit_seat) * (unsigned long)q);

				if (!strcmp(seat->label, "W-gunner") && h2v_seat_text_gunner == NONE)
					h2v_seat_text_gunner = seat->icon_text_index;
				if (!strcmp(seat->label, "W-passenger") && h2v_seat_text_passenger == NONE)
					h2v_seat_text_passenger = seat->icon_text_index;
			}
		}
		h2v_seats_write(r, root, v, h2v_own_collision(v) ? first + h2v_physics_tag(v) + 2 : NONE, first);
		h2v_root_values_write(r, root, v);
		/* (its bounding sphere over Halo 2's model, not the stand-in's: the
		Banshee's wings past the Ghost's sphere were never tested - bullets
		through them; the objects' collisions test the sphere first) */
		if (v->model.model != NONE && !getenv("HALO_H2_VEHICLE_BOUNDS_AS_IS"))
		{
			struct model_made const *made = &build->models[v->model.model];
			float bx = readf(region_at(r, root + offsetof(struct unit_definition, object.bounding_offset.x)));
			float by = readf(region_at(r, root + offsetof(struct unit_definition, object.bounding_offset.y)));
			float bz = readf(region_at(r, root + offsetof(struct unit_definition, object.bounding_offset.z)));
			float radius = readf(region_at(r, root + offsetof(struct unit_definition, object.bounding_radius)));
			float wanted = made->radius + (float)sqrt(bx * bx + by * by + bz * bz);

			if (wanted > radius)
			{
				writef(region_at(r, root + offsetof(struct unit_definition, object.bounding_radius)), wanted);
				if (readf(region_at(r, root + offsetof(struct unit_definition, object.render_bounding_radius))) < wanted)
					writef(region_at(r, root + offsetof(struct unit_definition, object.render_bounding_radius)), wanted);
				platform_log("h2 vehicles: %s: its bounding radius %.2f (the stand-in's %.2f)", v->name, wanted, radius);
			}
		}
		/* its collision model's damage (the stand-in's, this level's copy):
		Halo 2's vehicles smoke when hurt and blow up in fire when killed
		(the owner's MCC frames) - the frag grenade's smoke and fire
		explosion as its damaged and destroyed effects */
		{
			/* (the turret's is none: the stand-in's is the Warthog's) */
			long coll = h2v_own_collision(v) ? first + h2v_physics_tag(v) + 1 : !h2v_turret(v) ?
				(short)read16(region_at(r, root + offsetof(struct unit_definition, object.collision_model) + 12)) : NONE;
			unsigned long coll_root = coll >= 0 ? h2w_root(r, instances, coll) : 0;

			if (getenv("HALO_H2_DEBUG") && coll_root)
				fprintf(stderr, "h2 vehicles: %s collision flags %08lx body %.1f damaged %.2f (%d) depleted (%d) destroyed %.2f (%d)\n", v->name,
					(unsigned long)read32(region_at(r, coll_root + offsetof(struct damage_resistance, flags))),
					readf(region_at(r, coll_root + offsetof(struct damage_resistance, maximum_body_vitality))),
					readf(region_at(r, coll_root + offsetof(struct damage_resistance, body_damaged_effect_threshold))),
					(short)read16(region_at(r, coll_root + offsetof(struct damage_resistance, body_damaged_effect) + 12)),
					(short)read16(region_at(r, coll_root + offsetof(struct damage_resistance, body_depleted_effect) + 12)),
					readf(region_at(r, coll_root + offsetof(struct damage_resistance, body_destroyed_threshold))),
					(short)read16(region_at(r, coll_root + offsetof(struct damage_resistance, body_destroyed_effect) + 12)));
			if (getenv("HALO_H2_DEBUG"))
				fprintf(stderr, "h2 vehicles: %s vitality %.1f smoke %ld fire %ld\n", v->name, v->vitality, h2v.kit_smoke, h2v.kit_fire_explosion);
			h2v_collision_damage_write(r, instances, coll_root, v);
		}
		if (getenv("HALO_H2_DEBUG"))
		{
			int q;

			fprintf(stderr, "h2 vehicles: %s stand-in type %d speeds:", v->name, (int)(short)read16(region_at(r, root + 0x2F4)));
			for (q = 0; q < 9; q++)
				fprintf(stderr, " %.3f", readf(region_at(r, root + 0x2F8 + 4 * q)));
			fprintf(stderr, "\n");
		}
		/* (own body: the stand-in's physics, its centre of mass Halo 2's - but
as low as the stand-in's: a Banshee's, high over the Ghost's few mass
points, rolled it over on the ground - and its mass points - its hull
against the world - moved under it, level:
Halo 2's body turns about its own centre, which a Banshee's on a Ghost's
physics did not - its weight tipped it over on the ground) */
		if (h2v_kinds[v->kind].own_body && v->physics.has_center_of_mass)
		{
			long base_physics = (short)read16(region_at(r, root + offsetof(struct unit_definition, object.physics) + 12));
			unsigned long physics_root = base_physics >= 0 ? h2w_root(r, instances, base_physics) : 0;

			if (physics_root)
			{
				long body_index = first + h2v_body_tag(v), m, count;
				unsigned long body = h2w_tag_copy(r, physics_root, sizeof(struct physics_definition), 'phys'), points;
				float from[3], dx, dy;

				if (r->b.failed)
					break;
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * body_index + 20), r->link + body);
				from[0] = readf(region_at(r, body + offsetof(struct physics_definition, center_of_mass.x)));
				from[1] = readf(region_at(r, body + offsetof(struct physics_definition, center_of_mass.y)));
				from[2] = readf(region_at(r, body + offsetof(struct physics_definition, center_of_mass.z)));
				dx = v->physics.center_of_mass[0] - from[0];
				dy = v->physics.center_of_mass[1] - from[1];
				writef(region_at(r, body + offsetof(struct physics_definition, center_of_mass.x)), v->physics.center_of_mass[0]);
				writef(region_at(r, body + offsetof(struct physics_definition, center_of_mass.y)), v->physics.center_of_mass[1]);
				count = (long)read32(region_at(r, body + offsetof(struct physics_definition, mass_points)));
				points = read32(region_at(r, body + offsetof(struct physics_definition, mass_points) + 4));
				for (m = 0; m < count && points; m++)
				{
					unsigned long at = points - r->link + sizeof(struct mass_point_definition) * (unsigned long)m;

					writef(region_at(r, at + offsetof(struct mass_point_definition, position.x)),
						readf(region_at(r, at + offsetof(struct mass_point_definition, position.x))) + dx);
					writef(region_at(r, at + offsetof(struct mass_point_definition, position.y)),
						readf(region_at(r, at + offsetof(struct mass_point_definition, position.y))) + dy);
				}
				/* (its hull against the world Halo 2's: spheres filling its
				physics model's shapes - the Banshee's wings, cab and
				rudders - besides the stand-in's points (the Ghost's few,
				under its middle: the Banshee's wings went into walls and the
				ground). Each a point of no mass of its own: the ground's
				push on a point is the body's mass's, physics.c) */
				if (v->shape_count > 0 && count > 0 && points && !getenv("HALO_H2_STANDIN_HULL"))
				{
					unsigned char template_point[sizeof(struct mass_point_definition)];
					unsigned char made[MAXIMUM_MASS_POINTS_PER_PHYSICS][sizeof(struct mass_point_definition)];
					long total = count, sh;

					memcpy(template_point, region_at(r, points - r->link), sizeof(template_point));
					for (m = 0; m < count && m < MAXIMUM_MASS_POINTS_PER_PHYSICS; m++)
						memcpy(made[m], region_at(r, points - r->link + sizeof(struct mass_point_definition) * (unsigned long)m), sizeof(made[m]));
					for (sh = 0; sh < v->shape_count; sh++)
					{
						float const *box = v->shapes[sh];
						float radius = box[3] < box[4] ? box[3] : box[4];
						long n[3], i0, i1, i2;
						int c;

						radius = radius < box[5] ? radius : box[5];
						radius = radius < 0.08f ? 0.08f : radius > 0.5f ? 0.5f : radius;
						for (c = 0; c < 3; c++)
						{
							n[c] = (long)(box[3 + c] / radius + 0.5f);
							n[c] = n[c] < 1 ? 1 : n[c] > 3 ? 3 : n[c];
						}
						for (i0 = 0; i0 < n[0]; i0++)
							for (i1 = 0; i1 < n[1]; i1++)
								for (i2 = 0; i2 < n[2] && total < MAXIMUM_MASS_POINTS_PER_PHYSICS; i2++)
								{
									struct mass_point_definition *mp = (struct mass_point_definition *)made[total++];
									long idx[3];

									idx[0] = i0;
									idx[1] = i1;
									idx[2] = i2;
									memcpy(mp, template_point, sizeof(template_point));
									snprintf(mp->name, sizeof(mp->name), "h2 hull %ld", total - count);
									mp->powered_mass_point_index = -1;
									mp->model_node_index = 0;
									mp->relative_mass = 0.0f;
									mp->mass = 0.0f;
									mp->radius = radius;
									for (c = 0; c < 3; c++)
										(&mp->position.x)[c] = box[c] + box[3 + c] * ((2.0f * idx[c] + 1.0f) / (float)n[c] - 1.0f);
								}
					}
					points = region_block(r, body + offsetof(struct physics_definition, mass_points), total, sizeof(struct mass_point_definition));
					if (r->b.failed)
						break;
					memcpy(region_at(r, points), made, sizeof(struct mass_point_definition) * (unsigned long)total);
					platform_log("h2 vehicles: %s: its hull %ld spheres of Halo 2's physics model's %ld shapes besides the stand-in's %ld points",
						v->name, total - count, v->shape_count, count);
				}
				region_reference(r, root + offsetof(struct unit_definition, object.physics), 'phys', tag_id(body_index));
				platform_log("h2 vehicles: %s: its body the stand-in's, centre of mass %.3f %.3f %.3f (Halo 2's %.3f %.3f %.3f), %ld mass points moved %.3f %.3f",
					v->name, v->physics.center_of_mass[0], v->physics.center_of_mass[1], from[2], v->physics.center_of_mass[0],
					v->physics.center_of_mass[1], v->physics.center_of_mass[2],
					count, dx, dy);
			}
		}
		/* (own body with no centre of mass of Halo 2's - a campaign
vehicle made on a multiplayer level, HALO_H2_ALL_VEHICLES' gravity throne:
its physics not in the level's maps -: the stand-in's as it is; an empty
tag refused the whole level, "has no data") */
		if (h2v_kinds[v->kind].own_body && !read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2v_body_tag(v)) + 20)))
		{
			long base_physics = (short)read16(region_at(r, root + offsetof(struct unit_definition, object.physics) + 12));
			unsigned long physics_root = base_physics >= 0 ? h2w_root(r, instances, base_physics) : 0;
			unsigned long body = physics_root ? h2w_tag_copy(r, physics_root, sizeof(struct physics_definition), 'phys') :
				region_alloc(r, sizeof(struct physics_definition));

			if (r->b.failed)
				break;
			write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (first + h2v_body_tag(v)) + 20), r->link + body);
			platform_log("h2 vehicles: %s: no centre of mass of Halo 2's: its body the stand-in's as it is", v->name);
		}
		/* (own graph: h2v_graph_write; at load - h2_models_load.inc - only
		named here, made from the map as the level loads) */
		if (v->graph && h2w_graphs_at_load)
			region_reference(r, root + offsetof(struct unit_definition, object.animation_graph), 'antr', tag_id(first + h2v_graph_tag(v)));
		else if (v->graph)
			h2v_graph_write(r, instances, root, first + h2v_graph_tag(v), v);
		/* (its own collision model: it is hit, destroyed and collides as
Halo 2's is) */
		if (h2v_own_collision(v) && !h2v_turret(v))
			region_reference(r, root + offsetof(struct unit_definition, object.collision_model), 'coll', tag_id(first + h2v_physics_tag(v) + 1));
		/* (the anti-personnel turret: Halo 2's turret, type 6, fixed where
		it stands - no physics -, nothing to collide with - the stand-in's
		is a Warthog's -, its gunner's seat alone) */
		/* (the insertion pod: no physics of its own - the level's machine
		carries it down and leaves it standing where it lands, upright; on
		a Ghost's body it fell on its side) */
		if (!strcmp(v->name, "insertion_pod") && !getenv("HALO_H2_POD_PHYSICS"))
		{
			write16(region_at(r, root + 0x2F4), 6);
			region_reference(r, root + offsetof(struct unit_definition, object.physics), 0, 0xFFFFFFFFUL);
		}
		if (h2v_turret(v))
		{
			write16(region_at(r, root + 0x2F4), 6);
			region_reference(r, root + offsetof(struct unit_definition, object.physics), 0, 0xFFFFFFFFUL);
			/* (Halo 2's own collision model: it is hit and destroyed) */
			if (h2v_own_collision(v))
				region_reference(r, root + offsetof(struct unit_definition, object.collision_model), 'coll', tag_id(first + h2v_physics_tag(v) + 1));
			else
				region_reference(r, root + offsetof(struct unit_definition, object.collision_model), 0, 0xFFFFFFFFUL);
		}
		/* its gun: the stand-in's, or (a Gauss Warthog) one made from it */
		{
			long weapon_count = (long)read32(region_at(r, root + offsetof(struct unit_definition, unit.initial_weapons)));
			unsigned long weapons = read32(region_at(r, root + offsetof(struct unit_definition, unit.initial_weapons) + 4));

			if (weapon_count > 0 && weapons)
				gun = (short)read16(region_at(r, weapons - r->link + 12));
			if (h2v_kinds[v->kind].gun && gun >= 0 && (gun_root = h2w_root(r, instances, gun)) != 0)
			{
				unsigned long triggers;
				long base_projectile, stand_in_damage = NONE;

				gun_root = h2w_tag_copy(r, gun_root, sizeof(struct weapon_definition), 'weap');
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 1) + 20), r->link + gun_root);
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 1) + 4), 'item');
				write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 1) + 8), 'obje');
				region_reference(r, weapons - r->link, 'weap', tag_id(index + 1));
				triggers = h2w_block_copy(r, gun_root + offsetof(struct weapon_definition, weapon.triggers), sizeof(struct weapon_trigger_definition));
				/* (its firing effect: the stand-in chaingun's - its flash, its
				shells - with Halo 2's gauss cannon's sound in place of the
				chaingun's, every firing effect of its trigger) */
				{
					unsigned long field = triggers ? triggers + offsetof(struct weapon_trigger_definition, firing_effects) : 0;
					long effect_count = field ? (long)read32(region_at(r, field)) : 0, e;
					unsigned long elements = field ? read32(region_at(r, field + 4)) : 0;
					long stand_in_effect = effect_count > 0 && elements ?
						(short)read16(region_at(r, elements - r->link + H2W_FIRING_EFFECT_EFFECT + 12)) : NONE;

					if (v->gun_sound != NONE || stand_in_effect < 0)
						h2w_effect_make(r, instances, stand_in_effect, index + 4, v->gun_sound, first + h2_vehicles_tag_count());
					else
					{
						/* (no sound of Halo 2's: the stand-in's effect as it is) */
						unsigned long copy = h2w_tag_copy(r, h2w_root(r, instances, stand_in_effect), sizeof(struct effect_definition), 'effe');

						if (!r->b.failed)
							write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 4) + 20), r->link + copy);
					}
					for (e = 0; e < effect_count && elements && !r->b.failed; e++)
						region_reference(r, elements - r->link + H2W_FIRING_EFFECT_SIZE * e + H2W_FIRING_EFFECT_EFFECT, 'effe', tag_id(index + 4));
				}
				base_projectile = triggers ? (short)read16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile) + 12)) : NONE;
				/* (the gauss cannon's bolt: the kit's rocket's - its
				detonation, its smoke -, unseen, straight) */
				if (base_projectile >= 0 && h2w_root(r, instances, base_projectile))
					stand_in_damage = (short)read16(region_at(r, h2w_root(r, instances, base_projectile) +
						offsetof(struct projectile_definition, projectile.impact_damage) + 12));
				if (h2v.kit_rocket != NONE && h2v_kinds[v->kind].gun_bolt)
					base_projectile = h2v.kit_rocket;
				if (triggers && v->gun_rounds_per_second > 0.0f)
				{
					float maximum = v->gun_rounds_per_second_maximum > v->gun_rounds_per_second ? v->gun_rounds_per_second_maximum :
						v->gun_rounds_per_second;
					int q;

					writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, initial_rate_of_fire)), v->gun_rounds_per_second);
					writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, final_rate_of_fire)), maximum);
					write16(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectiles_per_shot)), 1);
					/* (what lights the Wraith's mortar charge: the illumination, weapons.c) */
					if (v->gun_illumination_recovery > 0.0f)
					{
						writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, illumination_recovery_time)),
							v->gun_illumination_recovery);
						writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, runtime_illumination_recovery_time)),
							1.0f / (v->gun_illumination_recovery * TICKS_PER_SECOND));
					}
					/* (the spin: a tick's share of it, as the tags' postprocess
					makes it - none, at once) */
					for (q = 0; q < 2 && !h2v_kinds[v->kind].gun_bolt; q++)
					{
						float time = v->gun_rate_times[q];

						writef(region_at(r, triggers + (q ? offsetof(struct weapon_trigger_definition, rate_of_fire_deceleration_time) :
							offsetof(struct weapon_trigger_definition, rate_of_fire_acceleration_time))), time);
						writef(region_at(r, triggers + (q ? offsetof(struct weapon_trigger_definition, runtime_rate_of_fire_deceleration_time) :
							offsetof(struct weapon_trigger_definition, runtime_rate_of_fire_acceleration_time))),
							time > 0.0f ? 1.0f / (time * 30.0f) : 1.0f);
					}
					if (!h2v_kinds[v->kind].gun_bolt && v->gun_error[1] > 0.0f)
					{
						writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile_error_angle_lower_bound)), v->gun_error[0]);
						writef(region_at(r, triggers + offsetof(struct weapon_trigger_definition, projectile_error_angle_upper_bound)), v->gun_error[1]);
					}
				}
				if (base_projectile >= 0 && h2w_root(r, instances, base_projectile))
				{
					long base_damage;

					projectile_root = h2w_tag_copy(r, h2w_root(r, instances, base_projectile), sizeof(struct projectile_definition), 'proj');
					write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 2) + 20), r->link + projectile_root);
					write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 2) + 4), 'obje');
					region_reference(r, triggers + offsetof(struct weapon_trigger_definition, projectile), 'proj', tag_id(index + 2));
					if (base_projectile == h2v.kit_rocket)
					{
						region_reference(r, projectile_root + offsetof(struct projectile_definition, object.model), 'mode', NONE);
						/* (seen as a blue bolt: the charged plasma bolt's
						attachments in place of the rocket's smoke) */
						if (h2v.kit_bolt != NONE && h2w_root(r, instances, h2v.kit_bolt))
						{
							unsigned long bolt = h2w_root(r, instances, h2v.kit_bolt);
							long count = (long)read32(region_at(r, bolt + offsetof(struct projectile_definition, object.attachments)));
							unsigned long from = read32(region_at(r, bolt + offsetof(struct projectile_definition, object.attachments) + 4));

							if (count > 0 && from)
							{
								unsigned long to = region_block(r, projectile_root + offsetof(struct projectile_definition, object.attachments),
									count, sizeof(struct object_attachment_definition));

								if (!r->b.failed)
									memcpy(region_at(r, to), region_at(r, from - r->link), (unsigned long)count * sizeof(struct object_attachment_definition));
							}
						}
						/* (the mortar's falls: Halo 2's gravity scale) */
						writef(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.air_gravity_scale)),
							h2v_kinds[v->kind].gun_arc ? v->gun_gravity : 0.0f);
						writef(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.guided_angular_velocity)), 0.0f);
					}
					if (v->gun_velocity > 0.0f)
					{
						writef(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.initial_velocity)), v->gun_velocity / 30.0f);
						writef(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.final_velocity)), v->gun_velocity / 30.0f);
					}
					base_damage = (short)read16(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage) + 12));
					/* (a rocket has none: the stand-in gun's bullet's, made Halo 2's;
					a shell neither - its damage its detonation's -: the kit
					bullet's, its impact Halo 2's) */
					if (base_damage < 0)
						base_damage = stand_in_damage;
					if (base_damage < 0)
						base_damage = h2v.kit_damage;
					if (base_damage >= 0 && h2w_root(r, instances, base_damage))
					{
						damage_root = h2w_tag_copy(r, h2w_root(r, instances, base_damage), sizeof(struct damage_effect_definition), 'jpt!');
						write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + 3) + 20), r->link + damage_root);
						if (v->gun_damage[2] > 0.0f)
						{
							/* (Halo 2's damage and its multipliers on the
							players, h2_damage.c) */
							writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_lower_bound)), v->gun_damage[0]);
							writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_upper_bound)), v->gun_damage[2]);
							writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, damage.damage_minimum)), v->gun_damage[0]);
							h2w_damage_modifiers_write(build, r, damage_root, v->gun_damage_tag);
						}
						if (v->gun_radius[1] > 0.0f)
						{
							writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, falloff_radius)), v->gun_radius[0]);
							writef(region_at(r, damage_root + offsetof(struct damage_effect_definition, cutoff_radius)), v->gun_radius[1]);
						}
						region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage), 'jpt!', tag_id(index + 3));
					}
					/* (the Scorpion's machine gun: its stand-in's bullet - the
					kit's, the Scorpion's alone - Halo 2's, in place) */
					if (v->gun2_impact_tag != NONE && (long)read32(region_at(r, gun_root + offsetof(struct weapon_definition, weapon.triggers))) > 1)
					{
						unsigned long trigger2 = triggers + sizeof(struct weapon_trigger_definition);
						long projectile2 = (short)read16(region_at(r, trigger2 + offsetof(struct weapon_trigger_definition, projectile) + 12));
						unsigned long projectile2_root = projectile2 >= 0 ? h2w_root(r, instances, projectile2) : 0;
						long bullet = projectile2_root ? (short)read16(region_at(r, projectile2_root +
							offsetof(struct projectile_definition, projectile.impact_damage) + 12)) : NONE;
						unsigned long bullet_root = bullet >= 0 ? h2w_root(r, instances, bullet) : 0;

						if (bullet_root && build->cache)
							h2_damage_effect_write(build->cache, v->gun2_impact_tag, region_at(r, bullet_root), h2w_write_real);
					}
					/* (its detonation: the stand-in's effect copied, its damage
					Halo 2's detonation's or none - the kit's shell's and
					rocket's explosions are this engine's, much more; with no
					impact of Halo 2's, the detonation alone) */
					if (!getenv("HALO_H2_VEHICLE_GUN_AS_IS"))
					{
						long stand_in_effect = (short)read16(region_at(r, projectile_root + offsetof(struct projectile_definition, projectile.effect) + 12));
						long effect_damage = h2w_effect_damage_part(r, instances, stand_in_effect);

						if (v->gun_detonation_tag != NONE)
							h2w_damage_make(build, r, instances, index + 5, effect_damage >= 0 ? effect_damage : h2v.kit_damage, v->gun_detonation_tag);
						if (stand_in_effect >= 0 && h2w_root(r, instances, stand_in_effect))
						{
							h2w_effect_damage_copy(r, instances, stand_in_effect, index + 6,
								v->gun_detonation_tag != NONE ? tag_id(index + 5) : 0xFFFFFFFFUL);
							region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.effect), 'effe', tag_id(index + 6));
						}
						if (!v->gun_impact && v->gun_detonation_tag != NONE)
							region_reference(r, projectile_root + offsetof(struct projectile_definition, projectile.impact_damage), 0, 0xFFFFFFFFUL);
					}
				}
				/* (a second barrel: a second trigger, the first's copied -
				its firing effect, its spread - firing the second barrel's
				bolt: the kit rocket's - its detonation, its explosion - with
				the plasma bolt's look, Halo 2's speed, gravity scale and
				guided turn rate, at its rate) */
				if (v->gun2_rate > 0.0f && triggers && h2v.kit_rocket != NONE && h2w_root(r, instances, h2v.kit_rocket) &&
					(long)read32(region_at(r, gun_root + offsetof(struct weapon_definition, weapon.triggers))) == 1)
				{
					unsigned long field = gun_root + offsetof(struct weapon_definition, weapon.triggers);
					unsigned char first_trigger[sizeof(struct weapon_trigger_definition)];
					unsigned long both, bomb;
					long bomb_index = first + h2v_gun2_tag(v), bomb_k;

					memcpy(first_trigger, region_at(r, triggers), sizeof(first_trigger));
					both = region_block(r, field, 2, sizeof(struct weapon_trigger_definition));
					if (!r->b.failed)
					{
						memcpy(region_at(r, both), first_trigger, sizeof(first_trigger));
						memcpy(region_at(r, both + sizeof(struct weapon_trigger_definition)), first_trigger, sizeof(first_trigger));
						triggers = both;
						both += sizeof(struct weapon_trigger_definition);
						/* (its firing effects its own copy: the tag check
						refuses two blocks over the same elements) */
						{
							unsigned long effects_field = both + offsetof(struct weapon_trigger_definition, firing_effects);
							long effect_count = (long)read32(region_at(r, effects_field));
							unsigned long effects_from = read32(region_at(r, effects_field + 4));
							unsigned char saved[H2W_FIRING_EFFECT_SIZE * 4];

							if (effect_count > 0 && effect_count <= 4 && effects_from)
							{
								unsigned long copy;

								memcpy(saved, region_at(r, effects_from - r->link), H2W_FIRING_EFFECT_SIZE * (unsigned long)effect_count);
								copy = region_block(r, effects_field, effect_count, H2W_FIRING_EFFECT_SIZE);
								if (!r->b.failed)
									memcpy(region_at(r, copy), saved, H2W_FIRING_EFFECT_SIZE * (unsigned long)effect_count);
							}
							else
								region_block(r, effects_field, 0, H2W_FIRING_EFFECT_SIZE);
						}
						/* (Halo 2's flag: its secondary trigger its own,
						"secondary trigger overrides grenades" - weapons.c
						fires a second trigger only so) */
						write32(region_at(r, gun_root + offsetof(struct weapon_definition, weapon.flags)),
							read32(region_at(r, gun_root + offsetof(struct weapon_definition, weapon.flags))) |
							FLAG(_weapon_secondary_trigger_overrides_grenades_bit));
						writef(region_at(r, both + offsetof(struct weapon_trigger_definition, initial_rate_of_fire)), v->gun2_rate);
						writef(region_at(r, both + offsetof(struct weapon_trigger_definition, final_rate_of_fire)), v->gun2_rate);
						write16(region_at(r, both + offsetof(struct weapon_trigger_definition, projectiles_per_shot)), 1);
						bomb = h2w_tag_copy(r, h2w_root(r, instances, h2v.kit_rocket), sizeof(struct projectile_definition), 'proj');
						if (!r->b.failed)
						{
							write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * bomb_index + 20), r->link + bomb);
							write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * bomb_index + 4), 'obje');
							region_reference(r, bomb + offsetof(struct projectile_definition, object.model), 'mode', NONE);
							if (h2v.kit_bolt != NONE && h2w_root(r, instances, h2v.kit_bolt))
							{
								unsigned long bolt = h2w_root(r, instances, h2v.kit_bolt);
								long count2 = (long)read32(region_at(r, bolt + offsetof(struct projectile_definition, object.attachments)));
								unsigned long from = read32(region_at(r, bolt + offsetof(struct projectile_definition, object.attachments) + 4));

								if (count2 > 0 && from)
								{
									unsigned long to = region_block(r, bomb + offsetof(struct projectile_definition, object.attachments), count2,
										sizeof(struct object_attachment_definition));

									if (!r->b.failed)
										memcpy(region_at(r, to), region_at(r, from - r->link), (unsigned long)count2 * sizeof(struct object_attachment_definition));
								}
							}
							writef(region_at(r, bomb + offsetof(struct projectile_definition, projectile.air_gravity_scale)), v->gun2_gravity);
							writef(region_at(r, bomb + offsetof(struct projectile_definition, projectile.guided_angular_velocity)), v->gun2_guided);
							if (v->gun2_velocity > 0.0f)
							{
								writef(region_at(r, bomb + offsetof(struct projectile_definition, projectile.initial_velocity)), v->gun2_velocity / 30.0f);
								writef(region_at(r, bomb + offsetof(struct projectile_definition, projectile.final_velocity)), v->gun2_velocity / 30.0f);
							}
							region_reference(r, both + offsetof(struct weapon_trigger_definition, projectile), 'proj', tag_id(bomb_index));
							/* (Halo 2's damage: its impact's and its explosion's -
							the kit rocket's explosion carrying the Banshee bomb's
							damage, h2w_projectile_write; its particles Halo 2's
							detonation's, h2_effects.c) */
							if (v->gun2_projectile != NONE && build->cache)
							{
								struct h2w_projectile p2;
								char bomb_name[96];

								h2w_projectile_read(build->cache, v->gun2_projectile, &p2);
								snprintf(bomb_name, sizeof(bomb_name), "h2\\vehicles\\%s gun bomb", v->name);
								h2w_projectile_write(build, r, instances, bomb, &p2, bomb_index + 1, bomb_index + 2 - H2W_SLOT_DETONATION_DAMAGE, bomb_name);
							}
							/* (its tags none was made for: empty) */
							for (bomb_k = 1; bomb_k < H2V_BOMB_TAGS && !r->b.failed; bomb_k++)
							{
								long slot = bomb_index + bomb_k;
								unsigned long empty;

								if (read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20)))
									continue;
								empty = bomb_k >= 2 + H2W_SLOT_DETONATION_EFFECT - H2W_SLOT_DETONATION_DAMAGE ? region_alloc(r, sizeof(struct effect_definition)) :
									region_alloc(r, sizeof(struct damage_effect_definition));
								if (!r->b.failed)
									write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * slot + 20), r->link + empty);
							}
						}
					}
				}
			}
		}
		/* (its damage sections: the game's, h2_vehicle_damage.c) */
		if (v->damage)
			h2_vehicle_damage_define(v->damage);
		/* (its gun's detonation tags none was made for: empty) */
		if (h2v_kinds[v->kind].gun)
		{
			long k;

			for (k = 5; k <= 6 && !r->b.failed; k++)
			{
				unsigned long empty;

				if (read32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + k) + 20)))
					continue;
				empty = k == 6 ? region_alloc(r, sizeof(struct effect_definition)) : region_alloc(r, sizeof(struct damage_effect_definition));
				if (!r->b.failed)
					write32(region_at(r, instances + XBOX_TAG_INSTANCE_SIZE * (index + k) + 20), r->link + empty);
			}
		}
		platform_log("h2 vehicles: %s made: %ld nodes, %ld parts, %ld regions%s", v->name, v->model.node_count,
			build->models[v->model.model].part_count, build->models[v->model.model].regions ? build->models[v->model.model].regions->count : 1L,
			h2v_kinds[v->kind].gun_bolt ? ", its gun Halo 2's gauss cannon" : h2v_kinds[v->kind].gun ? ", its gun Halo 2's" : "");
	}
	/* (the damage responses' blasts: Halo 2's damage effects) */
	{
		long b;

		for (b = 0; b < h2v.blast_count && !r->b.failed; b++)
			h2w_damage_make(build, r, instances, first + h2v_blast_first() + b, h2v.kit_blast != NONE ? h2v.kit_blast : h2v.kit_damage,
				h2v.blasts[b]);
	}
	h2v_debris_write(build, r, instances, first, model_first);
	/* (the scripts' damage effects: Halo 2's, on the kit's bullet - its
	screen flashes and camera shake Halo 2's player responses and shake) */
	{
		long t;

		for (t = 0; t < h2v.script_damage_count && !r->b.failed; t++)
			h2w_damage_make(build, r, instances, first + h2v_script_damage_first() + t, h2v.kit_damage, h2v.script_damages[t]);
	}
	/* (the boarders' blow on a hatch: Halo 2's damage) */
	if (h2v.board_melee != NONE && !r->b.failed)
		h2w_damage_make(build, r, instances, first + h2_vehicles_tag_count() - 1, h2v.kit_damage, h2v.board_melee);
}

H2_ALIGN static void h2_vehicles_dispose(void)
{
	long i;

	for (i = 0; i < h2v.count; i++)
	{
		h2w_graph_free(h2v.vehicles[i]->graph);
		free(h2v.vehicles[i]->damage);
		free(h2v.vehicles[i]);
	}
	h2v.count = 0;
	for (i = 0; i < h2v.debris_count; i++)
		free(h2v.debris[i]);
	h2v.debris_count = 0;
	h2v.board_melee = NONE;
	h2v.kit_rocket = NONE;
}

/* (h2v_values_map_loaded) the made vehicle's force model (the one string of
"<name> physics", h2_vehicle_physics.h) its plain values Halo 2's again:
those h2v_physics_read reads straight from the vehicle and its physics -
type, control, speeds, turning, slide, overdamping, engine, flying torque,
seat scales, boost, ground and slope, bank, gravity scale, radius, mass,
rider mass, turnover, flags, collision limits, gears, aim limits. Not what
it places by the model's markers and nodes (friction and anti-gravity
points, the hull, the death push, the centre of mass and damping): those
stay the level's. Only a force model of this version, whole */
/* whether the vehicle made under name (h2\\vehicles\\<collection>) is a Halo 3
one (h3_vehicles.c marks its force model's pad word) */
H2_ALIGN static boolean h2v_made_is_h3(char const *vehicle_name)
{
	char name[192];
	struct tag_iterator iterator;
	long index;

	snprintf(name, sizeof(name), "%s physics", vehicle_name);
	tag_iterator_new(&iterator, 'str#');
	while ((index = tag_iterator_next(&iterator)) != NONE)
	{
		struct tag_block const *strings;
		struct tag_data const *string;
		struct h2vp_definition const *to;

		if (!tag_get_name(index) || strcmp(tag_get_name(index), name))
			continue;
		strings = (struct tag_block const *)tag_get('str#', index);
		if (strings->count < 1 || !strings->address)
			return FALSE;
		string = (struct tag_data const *)strings->address;
		to = (struct h2vp_definition const *)string->address;
		return string->size == (long)sizeof(*to) && to && to->magic == H2VP_MAGIC && to->pad == 1;
	}
	return FALSE;
}

H2_ALIGN static void h2v_force_model_scalars_write(struct h2_cache *cache, struct h2v_vehicle *v, unsigned long address,
	char const *vehicle_name)
{
	char name[192];
	struct tag_iterator iterator;
	long index, list = NONE;
	struct tag_block const *strings;
	struct tag_data const *string;
	struct h2vp_definition *to;
	struct h2vp_definition const *from = &v->physics;
	unsigned char *before = NULL;

	snprintf(name, sizeof(name), "%s physics", vehicle_name);
	tag_iterator_new(&iterator, 'str#');
	while ((index = tag_iterator_next(&iterator)) != NONE && list == NONE)
	{
		if (tag_get_name(index) && !strcmp(tag_get_name(index), name))
			list = index;
	}
	if (list == NONE)
		return;
	strings = (struct tag_block const *)tag_get('str#', list);
	if (strings->count < 1 || !strings->address)
		return;
	string = (struct tag_data const *)strings->address;
	to = (struct h2vp_definition *)string->address;
	if (string->size != (long)sizeof(*to) || !to || to->magic != H2VP_MAGIC || to->version != H2VP_VERSION)
		return;
	/* (Halo 2's force model read as made, with no model: its markers'
	places none - only the plain values below are taken) */
	h2w_model_begin(&v->model);
	h2v_physics_quiet = TRUE;
	h2v_physics_read(cache, v, address);
	h2v_physics_quiet = FALSE;
	if (from->magic != H2VP_MAGIC)
		return;
	if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
		to->engine_momentum = 123.0f, to->mass = 1.0f;
	if (getenv("HALO_H2_WEAPON_VALUES_CHECK") && (before = malloc(sizeof(*to))) != NULL)
		memcpy(before, to, sizeof(*to));
#define H2V_TAKE(field) memcpy(&to->field, &from->field, sizeof(to->field))
	H2V_TAKE(vehicle_flags), H2V_TAKE(type), H2V_TAKE(control), H2V_TAKE(specific_type);
	H2V_TAKE(speed), H2V_TAKE(turn_angles), H2V_TAKE(wheel_circumference), H2V_TAKE(turn_rate), H2V_TAKE(slide);
	H2V_TAKE(overdampen_cusp), H2V_TAKE(overdampen_exponent), H2V_TAKE(turn_scale);
	H2V_TAKE(engine_momentum), H2V_TAKE(engine_maximum_angular_velocity), H2V_TAKE(flying_torque_scale);
	H2V_TAKE(seat_entrance_scale), H2V_TAKE(seat_exit_scale);
	H2V_TAKE(boost_peak_power), H2V_TAKE(boost_rise_power), H2V_TAKE(boost_peak_time), H2V_TAKE(boost_fall_power),
		H2V_TAKE(boost_dead_time);
	H2V_TAKE(physics_flags), H2V_TAKE(ground_friction), H2V_TAKE(ground_depth), H2V_TAKE(ground_damp),
		H2V_TAKE(ground_moving_friction), H2V_TAKE(slope), H2V_TAKE(bank_lift), H2V_TAKE(bank_reaction);
	H2V_TAKE(gravity_scale), H2V_TAKE(radius), H2V_TAKE(mass), H2V_TAKE(rider_mass), H2V_TAKE(turnover_time);
	H2V_TAKE(collision), H2V_TAKE(has_collision), H2V_TAKE(gear_count), H2V_TAKE(gears);
	H2V_TAKE(yaw_limits), H2V_TAKE(pitch_limits), H2V_TAKE(has_yaw_limits);
#undef H2V_TAKE
	if (before)
	{
		unsigned long changed = 0, b;

		for (b = 0; b < sizeof(*to); b++)
			changed += before[b] != ((unsigned char const *)to)[b];
		platform_log("h2 weapon values: check: %s: %lu bytes changed", name, changed);
		free(before);
	}
}

/* (h2_weapon_fields_map_loaded, as a level loads) each made vehicle's own
values Halo 2's again from the level's MCC map (h2v_root_values_write on
the loaded tags: a region over the tag cache); the vehicles written */
H2_ALIGN static long h2v_values_map_loaded(struct h2_cache *cache, struct region *loaded)
{
	struct h2v_vehicle *v = malloc(sizeof(*v));
	float splatter[4];
	long kind, written = 0;

	if (!v)
		return 0;
	/* (the players' collision damage limits, as h2_vehicles_gather reads them) */
	memset(splatter, 0, sizeof(splatter));
	{
		long biped = h2v_tag_named(cache, 'bipd', "objects\\characters\\masterchief\\masterchief_mp");
		struct h2_cache_tag t;
		int q;

		if (biped != NONE && h2_cache_tag_get(cache, biped, &t))
		{
			for (q = 0; q < 4; q++)
				splatter[q] = h2w_read_real(cache, t.address + 0x70 + 4 * q);
		}
	}
	memcpy(h2v.splatter, splatter, sizeof(splatter));
	for (kind = 0; kind < (long)(sizeof(h2v_kinds) / sizeof(h2v_kinds[0])); kind++)
	{
		char name[160];
		struct tag_iterator iterator;
		struct h2_cache_tag t;
		long index, made = NONE, source;
		unsigned char *root;
		unsigned char *before = NULL;

		snprintf(name, sizeof(name), "h2\\vehicles\\%s", h2v_kinds[kind].collection);
		tag_iterator_new(&iterator, 'vehi');
		while ((index = tag_iterator_next(&iterator)) != NONE && made == NONE)
		{
			if (tag_get_name(index) && !strcmp(tag_get_name(index), name))
				made = index;
		}
		if (made == NONE || (source = h2v_source_tag(cache, kind)) == NONE || !h2_cache_tag_get(cache, source, &t))
			continue;
		/* (a Halo 3 vehicle made under the name - its force model's pad word
		marks it, h3_vehicles.c -: its values are its own map's) */
		if (h2v_made_is_h3(name))
			continue;
		memset(v, 0, sizeof(*v));
		v->kind = kind;
		snprintf(v->name, sizeof(v->name), "%s", h2v_kinds[kind].collection);
		h2v_vehicle_values_read(cache, t.address, v);
		root = (unsigned char *)tag_get('vehi', made);
		if (getenv("HALO_H2_WEAPON_VALUES_PERTURB"))
			writef(root + offsetof(struct unit_definition, object.acceleration_scale), 77.0f);
		if (getenv("HALO_H2_WEAPON_VALUES_CHECK") && (before = malloc(0x400)) != NULL)
			memcpy(before, root, 0x400);
		h2v_root_values_write(loaded, (unsigned long)root - loaded->link, v);
		h2v_force_model_scalars_write(cache, v, t.address, name);
		written++;
		if (before)
		{
			long changed = 0, b;

			for (b = 0; b < 0x400; b++)
				changed += before[b] != root[b];
			platform_log("h2 weapon values: check: %s: %ld bytes changed", name, changed);
			free(before);
		}
	}
	free(v);
	return written;
}

#else

/* (built alone: nothing - h2_level.c compiles it, through h2_weapons.c) */
typedef int h2_vehicles_compiled_in_h2_level;

#endif
