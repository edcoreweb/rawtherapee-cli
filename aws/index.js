const fs = require('fs')
const S3 = require('aws-sdk/clients/s3')
const childProcess = require('child_process');

const s3 = new S3({
  apiVersion: '2006-03-01',
  region: process.env.REGION
})

function upload (key, src)  {
  const data = fs.readFileSync(src)
  
  return s3.putObject({
    Body: data,
    ContentType: 'image/jpg',
    Bucket: process.env.BUCKET,
    Key: key,
    ACL: 'public-read'
  }).promise()
}

async function download (key, dest) {
  const data = await s3.getObject({
    Bucket: process.env.BUCKET,
    Key: key
  }).promise()
  
  fs.writeFileSync(dest, data.Body)
}

exports.handler = async (event) => {
	await download('IMG_0416.CR2', '/tmp/IMG_0416.CR2')
	
	const out = childProcess.execSync('rawtherapee-cli-custom /tmp/IMG_0416.CR2 /tmp/out.jpg 2370 1740 79')
	
	await upload('IMG_0416.jpg', '/tmp/out.jpg')
	
	return out.toString()
}