/*
 * Clay configuration for the phone-side settings page.
 *
 * Token-based auth only: the user provides the SignalK server address and a
 * SignalK access token. These stay on the phone (used by src/pkjs to reach the
 * server) and are not sent to the watch.
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
      "Connect to your boat's SignalK server. Generate an access token in the " +
      "SignalK admin UI (Security &rarr; Access Requests / Devices) and paste it below.",
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
        messageKey: "token",
        label: "Access token",
        description: "SignalK access token (used as a Bearer token)",
        attributes: {
          placeholder: "paste token",
          limit: 1000,
          autocapitalize: "off",
          autocorrect: "off",
          spellcheck: "false",
        },
      },
    ],
  },
  {
    type: "submit",
    defaultValue: "Save",
  },
];
