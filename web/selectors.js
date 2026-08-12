// WhatsApp Web DOM selectors -- THE ONLY FILE THAT SHOULD NEED EDITING WHEN
// WHATSAPP CHANGES ITS DOM.
//
// WhatsApp ships obfuscated, generated class names (`.x1n2onr6`, `._ak8j`) that
// churn without notice. Everything here prefers, in order:
//   1. ARIA roles / semantic attributes  -- WhatsApp needs these for a11y, so they
//      are the most stable thing on the page.
//   2. data-* attributes                 -- semi-stable, survive most restyles.
//   3. Generated class names              -- last resort, expect breakage.
//
// Each category lists MULTIPLE candidate selectors. They are all applied; a
// selector matching nothing is harmless. This redundancy is deliberate: when one
// breaks, the others keep the blur up rather than leaking content.

window.__wapSelectors = {
  // Left-hand chat list: contact/group display names.
  names: [
    '#pane-side [role="listitem"] span[title]',
    '#pane-side [role="gridcell"] span[dir="auto"]',
    'header [data-testid="conversation-info-header"] span[title]',
    // Active-chat header name
    '#main header span[title]',
  ],

  // Message bubble text, in the open conversation.
  messages: [
    '#main .message-in .selectable-text',
    '#main .message-out .selectable-text',
    '#main [data-pre-plain-text]',
    '#main div.copyable-text span.selectable-text',
    // Quoted/replied-to text
    '#main [data-testid="quoted-message"]',
  ],

  // Avatars / profile photos, list and header alike.
  pictures: [
    'img[src^="blob:"]',
    'img[draggable="false"][src*="/pps/"]',
    '#pane-side [role="listitem"] img',
    'header img[alt=""]',
    '[data-testid="default-user"]',
    '[data-testid="default-group"]',
  ],

  // Chat-list secondary line: last-message preview, and media thumbnails
  // rendered inside the conversation.
  previews: [
    '#pane-side [role="listitem"] [role="gridcell"] span[dir="ltr"]',
    '#pane-side [role="listitem"] span[dir="auto"]:not([title])',
    '#main [data-testid="media-content"]',
    '#main .message-in img, #main .message-out img',
  ],
};
