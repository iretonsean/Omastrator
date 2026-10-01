# Analytics, SEO and AEO built in (backlog, 2026-09-30)

**Status: the author's idea, not scheduled.** Requested by the author on 2026-09-30.

## First step for whoever builds it: interview the author

Before any design or code, the agent that picks this up **interviews the author with AskUserQuestion** to decide
what this part of the product needs: which features, in what order, what the first version is, and what to leave
out. The list below is the author's starting point, not a spec. Write the answers into this file's "Decided"
section, then design (a `docs/ANALYTICS.md`), then build in phases with the usual review.

## What the author asked for

- **A lightweight analytics suite inside Omastrator.**
- **GA4 in one step:** the user enters a GA4 property ID (a measurement ID for the tag), and Omastrator puts the
  Google tag (gtag.js) into the project's site through the normal write-back (Save, commit, push).
- **Tagging from the live view:** in a Browser View frame (Live, Edit Page), the user selects a button or another
  element and tags it: a gtag event on click (or view, submit…), with a name and parameters. The tag is written
  into the project's code the way style edits are, and shows on the element in the frame.
- **Or an agent does it all:** "add analytics to this site" through the Omarchy default agent: it adds the tag,
  proposes the events worth tracking, and tags them, as a proposal the user keeps or discards.
- **SEO and AEO** (answer engine optimisation: how AI answer engines read and cite the site): audits of the live
  page (titles, meta, headings, structured data, links, speed signals, content that answer engines can quote) and
  fixes proposed into the code.
- **Data from outside:** the user's Google Analytics (GA4 Data API), and services such as DataForSEO (rankings,
  keywords, SERP and AI-overview data), shown in Omastrator next to the page they describe.
- **Guided by the Omarchy default agent:** it explains the numbers, suggests what to change, and makes the
  change as a proposal.

## Constraints to keep in mind

- **Credentials** (Google OAuth or a service account for the GA4 Data API, DataForSEO's login) never go in the
  project's repo, the document or a log: they live in the user's keyring or Omastrator's own config, as the cloud
  storage credentials do (docs/CLOUD-STORAGE.md).
- **Write-back** follows the same rules as Live's other edits: deterministic where it can be (a tag on an element
  with an id), otherwise through the agent, always reviewable and undoable.
- **Privacy:** tagging is the site owner's choice; Omastrator adds no tracking of its own.

## Decided

(Empty until the interview.)
