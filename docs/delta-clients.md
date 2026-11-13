# Delta clients: using our site from your own build

Delta is open (see [delta.md](delta.md), "An open network"). If you make a
fork, a port or a tool that speaks Delta, you're welcome to sign your players
in on our site with Quick Connect and use their Delta profiles. This page is
how to do that without stepping on anyone.

The short version: register your app, put its client ID in your build, ask
only for what you need, and keep your players' data private.

## What a client ID is for

When a device signs in by code, the player sees who is asking before they
approve it:

> Vita Port on Vita (Handheld), build 1.0, wants to sign in as @milenko.
> This is Vita Port, made by its own developers, not by Chupathingy. Its
> homepage: example.org.
> It wants to:
> - Show you as your profile in games
> - Send your games to your stats

The device then gets those scopes and nothing else. Its certificate and its
tokens carry them, and the site checks every request against them. On their
Devices page, players see which app each device came through and can revoke
any of them.

A client ID isn't a secret. Anyone can read it out of a build, and a build
that wants to lie can leave it out. What it does is let an honest app be
shown by name, be limited to what it asked for, and be switched off as a
group if something goes wrong. Players are still protected the usual way:
they approve every device themselves, and they can revoke it.

## Registering

1. Make a Delta profile on the site and sign in with your password (a
   session opened from the game can't register apps).
2. Open `/delta/clients` (For developers, in the site's footer) and fill in:
   - **Name**: your app's own name, 3 to 32 plain characters. It can't be
     or look like Chupathingy, Warthog or another app's name (we compare
     names the way they read, so `Chupath1ngy` counts). A name with "Delta"
     or "Chupa" in it, like "Deltarune Port", is held: you can register it,
     and an admin looks at the name when approving the app.
   - **Homepage**: an `https://` address. Players see its host on the
     approval page.
   - **Contact**: an email address or an `https://` address. Only the site's
     admins see it. We use it if your app is causing trouble.
   - **Platforms**: the platform keys your builds report (see
     [delta.md](delta.md), "Platform keys"). A device that reports another
     platform is refused. Need a new key? Ask us, as for a platform number.
   - **Scopes**: what your app needs (below). `moderate` isn't on the
     form: ask us once your app is approved.
3. You get a client ID like `vita-port-3fa91c`. A profile can register 3.

A new app is **pending**: its devices can sign in only to your own profile,
so you can test it. Once an admin approves it, anyone can sign in with it.
Ask on our Discord if it's been waiting a while.

A pending app keeps its name only from your own other apps. Two people can
register the same name; the first one we approve keeps it, and we rename or
delete the other. Don't register names you don't mean to ship.

You can't rename an approved app or widen its scopes yourself. Ask us, and
we'll change it from the admin side.

## Putting it in your build

Send `"client_id"` with the two requests that make a device:

- `POST /v1/identity/device/start` (Quick Connect)
- `POST /v1/identity/legacy/enroll` (a copy that still has a player key)

Optionally send `"scopes": ["play", "stats"]` to ask for less than your app
has. Leave it out to ask for all of your app's scopes.

If your fork builds from our code, it already sends a client ID:
`IDENTITY_CLIENT_ID` in `port/linux/src/identity.h`, which is
`"chupathingy"`. Build with your own:

    -DIDENTITY_CLIENT_ID='"vita-port-3fa91c"'

Please don't ship our ID. A request with no client ID, or with
`chupathingy`, is treated as Chupathingy itself.

Give your build its own device key, too. Our code keeps the key as
`delta_device.key` in the save root (`identity_slot_files` in
`identity.c`), and the save root is `ChupathingyCE` or `chupathingyce`
(`save_folder_default` in `save_folder.h`). A fork that keeps both finds the
player's Chupathingy key and tries to sign it in as your app. The site
refuses that with 409 `"other_client": true`: a device key stays with the
app it first signed in through. Change the save root's folder name to your
app's (best, so your saves stay apart too), or at least the key's file
name.

Everything else is the same as for our builds: the nonce, the signatures,
the poll, the confirm, refresh and tokens ([delta.md](delta.md), "Device
identity" and "Devices"). The `token` answer's `"scopes"` says what your
device may do right now.

## Scopes

Scopes are the device certificate's capability bits. Nothing new is added
to the certificate for apps; a device of yours just gets fewer bits.

| Scope | Bit | What players see | What it lets the device do |
| --- | --- | --- | --- |
| `play` | 0x01 | Show you as your profile in games | prove who the player is to hosts and servers |
| `stats` | 0x02 | Send your games to your stats | `/v1/claim`, `/v1/client_report` |
| `profile` | 0x04 | See your profile, change its privacy settings, and see your devices | `/v1/game/profile`, `/unlink`, `/privacy`, `/v1/link` (a site session that can't reach friends, clans or hosting without `social`), `/v1/identity/devices` |
| `social` | 0x10 | See your friends and clan, show them you're online, send invites, join parties, and list your games | `/v1/presence`, `/v1/social/*`, `/v1/party`, and with `profile`: `/v1/game/clan`, `/v1/game/friends`, and friends, clan and hosting in the site session `/v1/link` opens |
| `moderate` | 0x08 | Sign you in to moderate on servers where you have a role | moderation sign-in (the server still checks the role). Only we give it, to an approved app |
| `devices` | 0x20 | (never for apps) | approving and revoking other devices stays with Chupathingy's own builds |

What a device ends up with is the smallest of: what it asked for, what your
app has, and what any device would get anyway. Approving and revoking
devices never passes to an app, even when a player approves your device
from a password session.

If an admin narrows your app's scopes later, your devices' tokens follow at
once, and their certificates at the next refresh. A certificate carries the
capability bits but not your client ID, and lives up to 7 days (14 for a
console), so one issued before the change keeps its old bits until then.
Taking `profile` away also ends the site sessions your devices opened.

If you run a host or server that checks certificates, go by the capability
bits in each one and fetch the revocation list (`/v1/identity/revoked`,
[delta.md](delta.md)). That list is how a suspension reaches you.

## Limits

Each app has its own counts, on top of the usual per-address and per-device
ones:

| What | A new app's limit |
| --- | --- |
| Quick Connect codes | 300 an hour |
| Tokens | 3000 an hour |

Past them, the site answers 429 with `"client": "limit"`. If your player
base needs more, ask and we'll raise them.

## Errors your build should handle

All are JSON with an `"error"` you can show the player:

| Answer | Means |
| --- | --- |
| 403 `"client": "unknown"` | the client ID isn't registered here (another site, or a typo) |
| 403 `"client": "suspended"` | your app was suspended; its devices are revoked |
| 403 `"client": "platform"` | the device reported a platform your app didn't register |
| 403 `"client": "pending"` | your app isn't approved yet, and this isn't your profile |
| 400 `"client": "scopes"` | none of the scopes asked for are your app's |
| 429 `"client": "limit"` | your app's own count is used up for now |
| 409 `"other_client": true` | that device key already signed in through another app (often Chupathingy's own key in a shared save root); use a key of your own |

## The rules

By registering, you agree to these. We'll suspend an app that breaks them.

- **Use your own name.** Don't call your app Chupathingy, don't use our logo
  as yours, and don't make your approval look like ours. Saying it's built
  on Chupathingy or works with Delta is fine and welcome.
- **Keep players' data private.** What your app gets from our site is the
  player's to share, not yours to sell or publish. Don't show addresses,
  ever (our own rule: see [privacy.md](privacy.md)). Don't keep more than
  your app needs.
- **Say what you collect.** If your app collects anything on its own
  (analytics, crash reports, stats), say so plainly on your homepage, let
  players turn it off, and keep it secured. That's how we do it too.
- **Ask for what you need.** If you don't use friends, don't ask for
  `social`.
- **Don't get around the limits.** One client ID per app. Don't rotate IDs,
  hide behind ours, or leave yours out to get more.
- **Keep your contact working.** If something goes wrong, we write to it
  before we do anything, unless players are at risk right now.

Suspending an app revokes every device that came through it: its tokens
and the site sessions it opened stop at once, and the devices go on the
revocation list, which hosts and servers pick up within ten minutes. It
never touches the player's other devices. Approving it again doesn't bring
those devices back; players sign in again.

## Your own site

Nothing here makes our site special. If you'd rather run your own Delta
site, this spec is enough to build one, and your build can point at it
instead. A client ID from our site means nothing on another.
