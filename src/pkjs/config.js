/*
 * Clay configuration for the phone-side settings page.
 *
 * Auth is via the SignalK "access request" flow, so the user only provides the
 * server address (and an optional device description shown to the admin during
 * approval). The app requests a token automatically and the server admin
 * approves it — the user never pastes a token.
 *
 * The `messageKey` values here are Clay form-field identifiers used as the keys
 * of the persisted settings object — they are intentionally NOT Pebble
 * AppMessage keys and do not need to appear in package.json -> messageKeys.
 */

module.exports = [
  {
    type: "heading",
    defaultValue: "pebblek",
  },
  {
    type: "text",
    defaultValue:
      "Enter your boat's SignalK server address, then approve the access " +
      "request in the SignalK admin UI (Security &rarr; Access Requests).",
  },
  {
    type: "section",
    items: [
      {
        type: "heading",
        defaultValue: "SignalK server",
      },
      {
        type: "input",
        messageKey: "serverAddress",
        label: "Server address",
        description: "Host and port, e.g. 10.0.0.20:3000",
        attributes: {
          placeholder: "host:port",
          limit: 120,
          autocapitalize: "off",
          autocorrect: "off",
          spellcheck: "false",
        },
      },
      {
        type: "input",
        messageKey: "description",
        label: "Device name",
        description: "Shown on the server when approving access",
        attributes: {
          placeholder: "Pebble pebblek",
          limit: 60,
        },
      },
    ],
  },
  {
    type: "submit",
    defaultValue: "Save",
  },
];
