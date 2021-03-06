// helpers for testing repl sets
// run
//   mongo --shell testing.js

cfg = {
    _id: 'asdf',
    members: [
        { _id : 0, host : "dm_hp" },
        { _id : 2, host : "dm_hp:27002" }
        ]
};

db = db.getSisterDB("admin");
local = db.getSisterDB("local");

print("\nswitched to admin db\n");

function rc(c) { return db.runCommand(c); }
