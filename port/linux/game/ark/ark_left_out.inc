/*
ARK_LEFT_OUT.INC

(port) The parts of h2_level.c that this tree leaves out (they stay in
project-ark-dev; ark_left_out.c has the rest): what the weapons and
vehicles code compiled with it calls of them. Nothing is replaced and
nothing is made.
*/

static boolean h3_weapons_replaces(long kind)
{
	(void)kind;
	return FALSE;
}

static boolean h3_weapons_level_on(void)
{
	return FALSE;
}

static void h3_weapons_gather(struct build *build)
{
	(void)build;
}

static char const *h3_weapons_label(struct h2w_weapon const *w)
{
	(void)w;
	return NULL;
}

static void h3_weapons_damage_write(struct build *build, struct region *r, unsigned long instances, unsigned long projectile_root,
	unsigned long base_projectile_root, long slot, struct h2w_weapon const *w, int which)
{
	(void)build;
	(void)r;
	(void)instances;
	(void)projectile_root;
	(void)base_projectile_root;
	(void)slot;
	(void)w;
	(void)which;
}

static void h3_weapons_triggers_write(struct region *r, unsigned long root, unsigned long triggers, struct h2w_weapon const *w)
{
	(void)r;
	(void)root;
	(void)triggers;
	(void)w;
}

static void h3_weapons_melee_area_write(struct region *r, unsigned long instances, unsigned long root, long weapon_index,
	struct h2w_weapon const *w)
{
	(void)r;
	(void)instances;
	(void)root;
	(void)weapon_index;
	(void)w;
}

static boolean h3_weapons_grenades_h3(void)
{
	return FALSE;
}

static boolean h3_vehicles_replaces(long kind)
{
	(void)kind;
	return FALSE;
}

static void h3_vehicles_gather(struct build *build)
{
	(void)build;
}
