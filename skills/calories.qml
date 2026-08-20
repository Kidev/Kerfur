// SPDX-FileCopyrightText: 2026 Alexandre 'kidev' Poumaroux
// SPDX-License-Identifier: AGPL-3.0-or-later
//
// A worked example of everything a skill can do. Edit it and press Reload in
// the control panel; nothing is rebuilt and nothing restarts.

import Skills

Skill {
    id: day

    name: "calories"
    description: "Counts what has been eaten today against a daily limit"

    // Declared, so they exist before anybody has ever said anything, appear in
    // the panel, and have something to be reset to.
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

        onTriggered: day.set("caloriesOfTheDay", 0)
    }

    // "add three hundred calories", "i ate 450 calories", "log 200 calories".
    // The amount is a slot: whatever sits where {amount} is, converted to a
    // number whether it was spoken as digits or as words.
    Trigger {
        intent: "when asked about adding calories"
        phrases: ["add {amount} calories", "i ate {amount} calories", "log {amount} calories", "put {amount} calories"]

        onFired: slots => day.add("caloriesOfTheDay", slots.amount ?? 0)
    }

    // The same shape, plus a question. Nothing is written until the person who
    // asked says yes, and only that person's answer counts.
    Trigger {
        intent: "when asked to set the number of calories per day"
        phrases: ["set my daily calorie limit to {amount}", "set the number of calories per day to {amount}", "my calorie limit is {amount}"]
        confirm: "Are you sure you want to set the daily calorie limit to {amount}?"

        onFired: slots => day.set("maxCaloriesPerDay", slots.amount ?? day.number("maxCaloriesPerDay"))
    }

    // No slots, so the description alone is enough to match on.
    Trigger {
        intent: "when asked about the current calories"
        phrases: ["how many calories", "what is my calorie count", "current calories"]

        onFired: day.say("You are at <caloriesOfTheDay> of <maxCaloriesPerDay> calories")
    }

    // Reacting to a value rather than to a sentence. Raising an event is the
    // only way out of the skill sandbox: whatever bridges to house automation
    // listens on Skills.eventRaised and no skill file can reach it directly.
    Watch {
        key: "caloriesOfTheDay"

        onChanged: value => {
            if (value > day.number("maxCaloriesPerDay")) {
                day.emitEvent("calorieLimitExceeded", {
                    "total": value,
                    "limit": day.number("maxCaloriesPerDay")
                });
            }
        }
    }
}
