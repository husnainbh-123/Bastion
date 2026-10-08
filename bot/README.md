# Running Bastion as a Lichess bot

Lichess lets programs play through its [Bot API](https://lichess.org/api#tag/Bot).
The open-source bridge [lichess-bot](https://github.com/lichess-bot-devs/lichess-bot)
connects a bot account to any UCI engine, so Bastion can play anyone on Lichess.
Setup takes about fifteen minutes.

## 1. Create a bot account

1. Sign out of your normal account and create a **new** Lichess account for the bot,
   for example `BastionEngine`. It must not have played any games, because only
   unused accounts can become bot accounts.
2. While signed in as the bot, create a token with the **Play games with the bot
   API** permission: <https://lichess.org/account/oauth/token/create?scopes[]=bot:play&description=Bastion>.
   Copy it somewhere safe. It works like a password and Lichess shows it only once.

Keep the bot and your own account separate. Using an engine to help you in your
own games is cheating and gets accounts closed.

## 2. Install lichess-bot

You need Python 3.10 or newer and git.

**Windows (PowerShell)**

```powershell
git clone https://github.com/lichess-bot-devs/lichess-bot.git
cd lichess-bot
py -m venv venv
venv\Scripts\activate
pip install -r requirements.txt
```

**macOS / Linux**

```bash
git clone https://github.com/lichess-bot-devs/lichess-bot.git
cd lichess-bot
python3 -m venv venv
source venv/bin/activate
pip install -r requirements.txt
```

## 3. Add Bastion

1. Download the Bastion executable for your system from the
   [releases page](https://github.com/husnainbh-123/Bastion/releases) (choose
   `x86-64-v3` for a computer made after about 2015, `x86-64-v2` otherwise), or
   build it yourself (see the main README).
2. Put it in `lichess-bot/engines/` and rename it to `bastion` (`bastion.exe` on Windows).
3. Copy [`config.yml.example`](config.yml.example) from this folder to
   `lichess-bot/config.yml`, paste your token into the `token:` line and, on
   Windows, change `name: "bastion"` to `name: "bastion.exe"`.

## 4. Upgrade the account and start

```bash
python lichess-bot.py -u   # one time only: turns the account into a BOT account (cannot be undone)
python lichess-bot.py      # starts accepting challenges
```

Open `https://lichess.org/@/<your bot name>` and challenge it from another
account to check that it plays. Bots can play bullet, blitz, rapid and classical,
casual or rated.

## Keeping it online

The bot only plays while `lichess-bot.py` is running. Leaving it on a home
computer works; for a bot that is always available, run it on an always-on
machine such as a Raspberry Pi or a small cloud server. To let it look for games
on its own, set `allow_matchmaking: true` in `config.yml` and it will challenge
other bots when idle.

Settings worth knowing in `config.yml`:

| Setting | What it does |
| --- | --- |
| `uci_options: Threads` | CPU threads Bastion may use per game |
| `uci_options: Hash` | Memory for the transposition table, in MB |
| `uci_options: Move Overhead` | Time kept in reserve per move for network lag; raise it if the bot loses on time |
| `challenge: concurrency` | How many games to play at once |
| `challenge: time_controls` | Which time controls to accept |

Never commit `config.yml` anywhere: it contains the token. If a token leaks,
revoke it at <https://lichess.org/account/oauth/token> and create a new one.
