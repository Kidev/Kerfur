// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A worked example of everything a skill can do, and a complete one: it keeps
// its own count, answers every question about it and warns on its own when
// the limit is passed. Edit it from the control panel (Skills > Edit), or here
// and press Reload; nothing is rebuilt and nothing restarts.

import Skills

Skill {
    id: day

    // Where the day stands, out loud: "You are at 1200 of 2000 calories, 800
    // left". Shared by the triggers that change the count, so every change is
    // answered with the new total.
    function report(prefix) {
        const left = day.number("maxCaloriesPerDay") - day.number("caloriesOfTheDay");
        const where = left >= 0 ? "You are at <caloriesOfTheDay> of <maxCaloriesPerDay> calories, {left} left." : "You are at <caloriesOfTheDay> calories, {over} over your limit of <maxCaloriesPerDay>.";
        day.say((prefix ?? "") + where, {
            "left": left,
            "over": -left
        });
    }

    name: "calories"
    description: "Counts what has been eaten today against a daily limit"

    // Declared, so they exist before anybody has ever said anything, appear in
    // the panel, and have something to be reset to. Both survive a restart.
    Variable {
        key: "caloriesOfTheDay"
        defaultValue: 0
    }

    Variable {
        key: "maxCaloriesPerDay"
        defaultValue: 2000
    }

    // Runs whether or not anybody is talking. Wall clock, so it happens at
    // midnight rather than twenty four hours after the last restart.
    Routine {
        dailyAt: "00:00"

        onTriggered: day.reset("caloriesOfTheDay")
    }

    // "add three hundred calories", "i ate 450 calories", "log 200 calories".
    // The amount is a slot: whatever sits where {amount} is, converted to a
    // number whether it was spoken as digits or as words.
    Trigger {
        intent: "when asked about adding calories"
        phrases: ["add {amount} calories", "i ate {amount} calories", "i had {amount} calories", "log {amount} calories", "put {amount} calories"]

        onFired: slots => {
            const amount = Number(slots.amount ?? 0);
            if (!(amount > 0)) {
                day.say("I did not catch how many calories.");
                return;
            }
            day.add("caloriesOfTheDay", amount);
            day.report("Added " + amount + ". ");
        }
    }

    // The way back from a mistake. The count stops at zero.
    Trigger {
        intent: "when asked to remove calories"
        phrases: ["remove {amount} calories", "take off {amount} calories", "subtract {amount} calories"]

        onFired: slots => {
            const amount = Number(slots.amount ?? 0);
            if (!(amount > 0)) {
                day.say("I did not catch how many calories.");
                return;
            }
            day.set("caloriesOfTheDay", Math.max(0, day.number("caloriesOfTheDay") - amount));
            day.report("Removed " + amount + ". ");
        }
    }

    // The same shape, plus a question. Nothing is written until the person who
    // asked says yes, and only that person's answer counts.
    Trigger {
        intent: "when asked to set the number of calories per day"
        phrases: ["set my daily calorie limit to {amount}", "set the number of calories per day to {amount}", "my calorie limit is {amount}"]
        confirm: "Are you sure you want to set the daily calorie limit to {amount}?"

        onFired: slots => {
            const amount = Number(slots.amount ?? 0);
            if (!(amount > 0)) {
                day.say("A daily limit has to be more than zero.");
                return;
            }
            day.set("maxCaloriesPerDay", amount);
            day.report("Your daily limit is now " + amount + ". ");
        }
    }

    Trigger {
        intent: "when asked to reset the calories of the day"
        phrases: ["reset my calories", "reset the calories", "start the day over"]
        confirm: "Reset today's count of <caloriesOfTheDay> calories to zero?"

        onFired: {
            day.reset("caloriesOfTheDay");
            day.say("Today's count is back to zero.");
        }
    }

    // No slots, so the description alone is enough to match on. Anybody may
    // ask: answering a question changes nothing.
    Trigger {
        authority: Trigger.Anyone
        intent: "when asked about the current calories"
        phrases: ["how many calories", "what is my calorie count", "current calories", "how many calories are left"]

        onFired: day.report()
    }

    // Reacting to a value rather than to a sentence: the count crossing the
    // limit, once per crossing, whichever trigger or routine moved it.
    Watch {
        // The count this watch last saw, so a crossing is told apart from
        // every later addition past the limit.
        property real previous: day.number("caloriesOfTheDay")

        key: "caloriesOfTheDay"

        onChanged: value => {
            const limit = day.number("maxCaloriesPerDay");
            if (value > limit && previous <= limit) {
                day.say("That is past your daily limit of <maxCaloriesPerDay> calories.");
                // Also raised as an event. Something outside Kerfur (a light,
                // a house automation bridge) may listen on Skills.eventRaised;
                // this skill does not need anything to.
                day.emitEvent("calorieLimitExceeded", {
                    "total": value,
                    "limit": limit
                });
            }
            previous = value;
        }
    }
}
