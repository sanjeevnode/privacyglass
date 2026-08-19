// WhatsApp Web DOM selectors -- THE ONLY FILE THAT SHOULD NEED EDITING WHEN
// WHATSAPP CHANGES ITS DOM.
//
// Verified against the live DOM with Ctrl+Shift+D (writes diagnostics.txt with
// a match count for every selector below). A selector reporting n:0 is dead --
// check it there before trusting it.
//
// Findings that shaped this file:
//   * The chat list uses [role="gridcell"], NOT [role="listitem"]. Every
//     listitem-based rule matched nothing.
//   * Avatars are <img src="https://pps.whatsapp.net/...">, not blob: URLs.
//   * Message bubbles use neither .message-in/.message-out nor .selectable-text
//     in the current build; .copyable-text is also absent.

window.__wapSelectors = {
  // Contact/group display names: chat list rows and the open chat's header.
  names: [
    // Innermost titled span only. A bare span[title][dir="auto"] also matches
    // the row wrapper that CONTAINS the preview text, and an ancestor's blur
    // rasterizes its children -- so the Previews checkbox could never reveal
    // them. Verified with Ctrl+Shift+D: that rule accounted for 76 previews
    // being blurred by a name ancestor.
    // :not(:has(span)) -- not :has(span[title]) -- because the wrapper this must
    // exclude contains the *preview* spans, which carry no title of their own.
    '#pane-side [role="gridcell"] span[title]:not(:has(span))',
    '#main header span[title]:not(:has(span[title]))',
    '#main header span[dir="auto"]:not(:has(span))',
  ],

  // Message bubble text in the open conversation. WhatsApp's class names here
  // are generated and unstable, so key off structure and the copy attributes
  // that survive restyles.
  messages: [
    '#main [data-pre-plain-text]',
    '#main .selectable-text',
    '#main .copyable-text',
    '#main [role="row"] span[dir="ltr"]',
    '#main [role="row"] span[dir="auto"]',
    // Document cards and link previews are separate widgets inside the bubble.
    '#main [role="row"] a[href]',
    '#main [role="row"] [title]',
  ],

  // Avatars / profile photos.
  pictures: [
    'img[src*="pps.whatsapp.net"]',               // verified: 40 matches
    'img[src*="cdn.whatsapp.net"]',
    'img[src^="blob:"]',
    '#pane-side [role="gridcell"] img',
    '#main header img',
    // Fallback silhouettes when a contact has no photo.
    '[data-icon="default-user"]',
    '[data-icon="default-group"]',
    '[data-icon="default-user-wrapped"]',
  ],

  // Chat-list secondary line (last-message preview) and in-chat media.
  previews: [
    // Innermost text spans only. A bare span[dir="auto"] also matches the row
    // wrapper, and blurring that ancestor drags the avatar down with it.
    '#pane-side [role="gridcell"] span[dir="ltr"]:not(:has(span))',
    '#pane-side [role="gridcell"] span[dir="auto"]:not([title]):not(:has(span))',
    '#main [role="row"] img',
  ],
};

// Elements that must NEVER be blurred, even when inside a blurred container.
window.__wapExempt = [
  '[data-icon="muted"]',
  '[data-icon="pinned"]',
  '[data-icon="status-dblcheck"]',
  '[data-icon="status-check"]',
  '[data-icon="status-time"]',
  '[aria-label*="unread"]',
];
