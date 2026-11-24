import { passkeys } from './api'

// Windows Hello on the HTTPS LAN page: the browser's WebAuthn calls around daemon/Passkeys.h. The
// server checks everything; this only converts between base64url and the browser's buffers.

const toBuffer = (b64url: string): ArrayBuffer => {
  const b64 = b64url.replace(/-/g, '+').replace(/_/g, '/')
  const bin = atob(b64 + '='.repeat((4 - (b64.length % 4)) % 4))
  const bytes = new Uint8Array(bin.length)
  for (let i = 0; i < bin.length; i++) bytes[i] = bin.charCodeAt(i)
  return bytes.buffer
}

const toB64url = (buffer: ArrayBuffer): string => {
  let bin = ''
  for (const b of new Uint8Array(buffer)) bin += String.fromCharCode(b)
  return btoa(bin).replace(/\+/g, '-').replace(/\//g, '_').replace(/=+$/, '')
}

// Passkeys need a secure page (https) and a browser that has them.
export const windowsHelloPossible = () => window.isSecureContext && typeof window.PublicKeyCredential !== 'undefined'

// A new key on this PC, with the setup code made over Tailscale. Unlocks this session too.
export async function setUpWindowsHello(code: string, name: string): Promise<string> {
  const o = await passkeys.registerOptions(code)
  const credential = (await navigator.credentials.create({
    publicKey: {
      challenge: toBuffer(o.challenge),
      rp: { id: o.rp_id, name: 'Persely' },
      user: { id: toBuffer(o.user.id), name: o.user.name, displayName: o.user.name },
      pubKeyCredParams: [
        { type: 'public-key', alg: -7 }, // ES256
        { type: 'public-key', alg: -257 }, // RS256 - what Windows Hello often makes
      ],
      authenticatorSelection: { authenticatorAttachment: 'platform', userVerification: 'required', residentKey: 'discouraged' },
      excludeCredentials: o.exclude.map((id) => ({ type: 'public-key', id: toBuffer(id) })),
      attestation: 'none',
      timeout: 120000,
    },
  })) as PublicKeyCredential | null
  if (!credential) throw new Error('Windows Hello was cancelled')
  const response = credential.response as AuthenticatorAttestationResponse
  const publicKey = response.getPublicKey()
  if (!publicKey) throw new Error("This browser didn't hand over the new key - try Edge or Chrome")
  const r = await passkeys.register({
    code,
    name,
    id: toB64url(credential.rawId),
    public_key: toB64url(publicKey),
    alg: response.getPublicKeyAlgorithm(),
    client_data: toB64url(response.clientDataJSON),
    authenticator_data: toB64url(response.getAuthenticatorData()),
  })
  return r.login
}

// Unlocks editing in this session with a key set up before.
export async function unlockWithWindowsHello(): Promise<string> {
  const o = await passkeys.loginOptions()
  const credential = (await navigator.credentials.get({
    publicKey: {
      challenge: toBuffer(o.challenge),
      rpId: o.rp_id,
      allowCredentials: o.allow.map((id) => ({ type: 'public-key', id: toBuffer(id) })),
      userVerification: 'required',
      timeout: 120000,
    },
  })) as PublicKeyCredential | null
  if (!credential) throw new Error('Windows Hello was cancelled')
  const response = credential.response as AuthenticatorAssertionResponse
  const r = await passkeys.login({
    id: toB64url(credential.rawId),
    client_data: toB64url(response.clientDataJSON),
    authenticator_data: toB64url(response.authenticatorData),
    signature: toB64url(response.signature),
  })
  return r.login
}
